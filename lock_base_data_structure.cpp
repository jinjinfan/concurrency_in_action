#include <iostream>
#include <mutex>
#include <memory>
#include <condition_variable>
#include <map>
#include <list>
#include <algorithm>
#include <shared_mutex>
#include <vector>
using namespace std;

//Enabling concurrency by separating data
// Waiting for an item to pop
template<typename T>
class threadsafe_queue
{
private:
    struct node
    {
      shared_ptr<T> data;
      unique_ptr<node> next;
    };
    std::mutex head_mutex;
    std::mutex tail_mutex;
    std::unique_ptr<node> head;
    node* tail;
    std::condition_variable data_cond;

    node* get_tail()
    {
      std::lock_guard tail_lock{tail_mutex};
      return tail;
    }

    std::unique_ptr<node> pop_head()
    {
      std::unique_ptr<node> const old_head=std::move(head);
      head=std::move(old_head->next);
      return old_head;
    }

    std::unique_lock<std::mutex> wait_for_data()
    {
        std::unique_lock<std::mutex> head_lock(head_mutex);
        data_cond.wait(head_lock,[&]{return head!= get_tail();});
        return std::move(head_lock);
    }

    std::unique_ptr<node> wait_pop_head()
    {
        std::unique_lock<std::mutex> head_lock(wait_for_data());
        return pop_head();
    }

    std::unique_ptr<node> wait_pop_head(T& value)
    {
        std::unique_lock<std::mutex> head_lock(wait_for_data());
        value=std::move(*head->data);
        return pop_head();
    }

    std::unique_ptr<node> try_pop_head()
    {
        std::lock_guard<std::mutex> head_lock(head_mutex);
        if(head.get()==get_tail())
        {
            return std::unique_ptr<node>();
        }
        return pop_head();
    }

    std::unique_ptr<node> try_pop_head(T& value)
    {
        std::lock_guard<std::mutex> head_lock(head_mutex);
        if(head.get()==get_tail())
        {
            return std::unique_ptr<node>();
        }
        value=std::move(*head->data);
        return pop_head();
    }

public:
    threadsafe_queue():
        head(new node),tail(head.get())
    {}

    threadsafe_queue(const threadsafe_queue& other)=delete;
    threadsafe_queue& operator=(const threadsafe_queue& other)=delete;

    std::shared_ptr<T> try_pop()
    {
      std::unique_ptr<node> old_head=pop_head();
      return old_head?old_head->data:std::make_shared<T>();
    }
    
    bool try_pop(T& value)
    {
        std::unique_ptr<node> const old_head=try_pop_head(value);
        return old_head;
    }
    std::shared_ptr<T> wait_and_pop()
    {
      std::unique_ptr<node> const old_head = wait_pop_head();
      return old_head->data();

    }
    void wait_and_pop(T& value)
    {
      std::unique_ptr<node> const old_head = wait_pop_head(value);
    }
    void push(T new_value)
    {
      shared_ptr<T> new_data{make_shared<T>(std::move(new_value))};
      std::unique_ptr<node> p(new node);
      {
        node* const new_tail=p.get();
        std::lock_guard tail_lock(tail_mutex);
        tail->data=new_data;
        tail->next = std::move(p);
        tail = new_tail;
      }
      data_cond.notify_one();
    }
    bool empty()
    {
        std::lock_guard<std::mutex> head_lock(head_mutex);
        return (head==get_tail());
    }
};

template<typename Key,typename Value,typename Hash=std::hash<Key> >
class threadsafe_lookup_table
{
private:
    class bucket_type
    {
    private:
        typedef std::pair<Key,Value> bucket_value;
        typedef std::list<bucket_value> bucket_data;
        typedef typename bucket_data::iterator bucket_iterator;

        bucket_data data;
        mutable std::shared_mutex mutex;

        bucket_iterator find_entry_for(Key const& key) const
        {
          return find_if(data.begin(), data.end(),
              [&](bucket_value const& item)
              {
                return item.first == key;
              });
        }
    public:
        Value value_for(Key const& key,Value const& default_value) const
        {
            std::shared_lock<std::shared_mutex> lock(mutex);
            bucket_iterator const found_entry=find_entry_for(key);
            return (found_entry==data.end())?
                default_value : found_entry->second;
        }

        void add_or_update_mapping(Key const& key,Value const& value)
        {
            std::unique_lock<std::shared_mutex> lock(mutex);
            bucket_iterator const found_entry=find_entry_for(key);
            if(found_entry==data.end())
            {
                data.push_back(bucket_value(key,value));
            }
            else
            {
                found_entry->second=value;
            }
        }
    
        void remove_mapping(Key const& key)
        {
            std::unique_lock<std::shared_mutex> lock(mutex);
            bucket_iterator const found_entry=find_entry_for(key);
            if(found_entry!=data.end())
            {
                data.erase(found_entry);
            }
        }
    };
    
    std::vector<std::unique_ptr<bucket_type> > buckets;
    Hash hasher;

    bucket_type& get_bucket(Key const& key) const
    {
        std::size_t const bucket_index=hasher(key)%buckets.size();
        return *buckets[bucket_index];
    }

public:
    typedef Key key_type;
    typedef Value mapped_type;
    typedef Hash hash_type;
    
    threadsafe_lookup_table(
        unsigned num_buckets=19, Hash const& hasher_=Hash()):
        buckets(num_buckets),hasher(hasher_)
    {
        for(unsigned i=0;i<num_buckets;++i)
        {
            buckets[i].reset(new bucket_type);
        }
    }

    threadsafe_lookup_table(threadsafe_lookup_table const& other)=delete;
    threadsafe_lookup_table& operator=(
        threadsafe_lookup_table const& other)=delete;
    
    Value value_for(Key const& key,
        Value const& default_value=Value()) const
    {
        return get_bucket(key).value_for(key,default_value);
    }
    
    void add_or_update_mapping(Key const& key,Value const& value)
    {
        get_bucket(key).add_or_update_mapping(key,value);
    }
    
    void remove_mapping(Key const& key)
    {
        get_bucket(key).remove_mapping(key);
    }
  
  std::map<Key,Value> get_map() const
  {
    std::vector<std::unique_lock<std::shared_mutex> > locks;
    for(unsigned i=0;i<buckets.size();++i)
    {
        locks.push_back(
           std::unique_lock<std::shared_mutex>(buckets[i].mutex));
    }
    std::map<Key,Value> res;
    for(unsigned i=0;i<buckets.size();++i)
    {
        for(auto it=buckets[i].data.begin();
            it!=buckets[i].data.end();
            ++it)
        {
            res.insert(*it);
        }
    }
    return res;
}
};

template<typename T>
class threadsafe_list
{
    struct node
    {
        std::mutex m;
        std::shared_ptr<T> data;
        std::unique_ptr<node> next;

        node():next()
        {}
        
        node(T const& value):
            data(std::make_shared<T>(value))
        {}
    };
    node head;
public:
    threadsafe_list(){}

    ~threadsafe_list()
    {
      remove_if([](T const&) {return true;});
    }

    threadsafe_list(threadsafe_list const& other)=delete;
    threadsafe_list& operator=(threadsafe_list const& other)=delete;
    
    void push_front(T const& value)
    {
        std::unique_ptr<node> new_node(new node(value));
        std::lock_guard<std::mutex> lk{head.m};
        new_node->next=std::move(head.next);
        head.next=std::move(new_node);
    }

    template<typename Function>
    void for_each(Function f)
    {
        node* current=&head;
        std::unique_lock<std::mutex> lk(head.m);
        while(node* const next = current->next.get())
        {
            std::unique_lock<std::mutex> next_lk(next->m);
            lk.unlock();
            f(*next->data);
            current = next;
            lk = std::move(next_lk);
        }
    }

    template<typename Predicate>
    std::shared_ptr<T> find_first_if(Predicate p)
    {
        node* current=&head;
        std::unique_lock<std::mutex> lk(head.m);
        while(node* const next=current->next.get())
        {
            std::unique_lock<std::mutex> next_lk(next->m);
            lk.unlock();
            if(p(*next->data))
            {
                return next->data;
            }
            current=next;
            lk=std::move(next_lk);
        }
        return std::shared_ptr<T>();
    }

    template<typename Predicate>
    void remove_if(Predicate p)
    {
        node* current=&head;
        std::unique_lock<std::mutex> lk(head.m);
        while(node* const next=current->next.get())
        {
            std::unique_lock<std::mutex> next_lk(next->m);
            if(p(*next->data))
            {
                unique_ptr<node> old_next = std::move(current->next);
                current->next=std::move(next->next);
                next_lk.unlock();
            }
            else
            {
                lk.unlock();
                current=next;
                lk=std::move(next_lk);
            }
        }
    }
};

int main()
{
  threadsafe_queue<int> q;
  return 0;
}
