#include <iostream>
#include <mutex>
#include <memory>
#include <condition_variable>
#include <atomic>
#include <list>
#include <algorithm>
#include <shared_mutex>
#include <vector>
#include <thread>
using namespace std;

/*
Obstruction-Free
  If all other threads are paused, then any given thread will complete its operation 
  in a bounded number of steps.
Lock-Free
  If multiple threads are operating on a data structure, then after a bounded number of steps 
  one of them will complete its operation.
Wait-Free
 Every thread operating on a data structure will complete its operation in a bounded
 number of steps, even if other threads are also operating on the data structure.
*/

// lock free data structure
/* enable maximum concurrency
// robustness
// Live lock occurs 
// when two threads each try to change the data structure, but for each thread, the changes 
  made by the other require the operation to be restarted, so both threads loop and try again.
  it can increase the potential for concurrency of operations on a data structure and reduce 
  the time an individual thread spends waiting, it may well decrease overall performance. 
  First, the atomic operations used for lock-free code can be much slower than non-atomic operations, 
  and there’ll likely be more of them in a lock-free data structure than in the mutex locking code 
  for a lock-based data structure. Not only that, but the hardware must synchronize data between 
  threads that access the same atomic variables. 
*/
template<typename T>
class lock_free_stack
{
private:
    struct node
    {
        std::shared_ptr<T> data;
        node* next;
        node(T const& data_):
            data(std::make_shared<T>(data_))
        {}
    };
    std::atomic<node*> head;
public:
    void push(T const& data)
    {
      node* const new_node = new node{data};
      new_node->next = head.load();
      while(!head.compare_exchange_weak(new_node->next, new_node));
    }
    std::shared_ptr<T> pop()
    {
        node* old_head=head.load();
        while(old_head &&
              !head.compare_exchange_weak(old_head, old_head->next));
        return old_head ? old_head->data : std::shared_ptr<T>();
    }
};

template<typename T>
class lock_free_stack_with_gc // garbage collection
{
private:
    struct node
    {
        std::shared_ptr<T> data;
        node* next;
        node(T const& data_):
            data(std::make_shared<T>(data_))
        {}
    };
    std::atomic<node*> head;
    std::atomic<unsigned> threads_in_pop;
    std::atomic<node*> to_be_deleted;
    static void delete_nodes(node* nodes)
    {
        while(nodes)
        {
            node* next=nodes->next;
            delete nodes;
            nodes=next;
        }
    }
    void try_reclaim(node* old_head)
    {
        if(threads_in_pop==1)
        {
            node* nodes_to_delete=to_be_deleted.exchange(nullptr);
            if(!--threads_in_pop)
            {
                delete_nodes(nodes_to_delete);
            }
            else if(nodes_to_delete)
            {
                chain_pending_nodes(nodes_to_delete);
            }
            delete old_head;
        }
        else
        {
            chain_pending_node(old_head);
            --threads_in_pop;
        }
    }
    void chain_pending_nodes(node* nodes)
    {
        node* last=nodes;
        while(node* const next=last->next)
        {
            last=next;
        }
        chain_pending_nodes(nodes,last);
    }
    void chain_pending_nodes(node* first,node* last)
    {
        last->next=to_be_deleted;
        while(!to_be_deleted.compare_exchange_weak(
                  last->next,first));
    }
    void chain_pending_node(node* n)
    {
        chain_pending_nodes(n,n);
    }
public:
    std::shared_ptr<T> pop()
    {
        ++threads_in_pop;
        node* old_head=head.load();
        while(old_head &&
              !head.compare_exchange_weak(old_head,old_head->next));
        std::shared_ptr<T> res;
        if(old_head)
        {
            res.swap(old_head->data);
        }
        try_reclaim(old_head);
        return res;
    }
};

/*
Atomic operations are inherently slow—often 100 times slower than an
equivalent non-atomic operation on desktop CPUs—so this makes pop() 
an expensive operation. 
*/
unsigned const max_hazard_pointers=100;
struct hazard_pointer
{
    std::atomic<std::thread::id> id;
    std::atomic<void*> pointer;
};
hazard_pointer hazard_pointers[max_hazard_pointers];
class hp_owner
{
    hazard_pointer* hp;
public:
    hp_owner(hp_owner const&)=delete;
    hp_owner operator=(hp_owner const&)=delete;
    hp_owner():
        hp(nullptr)
    {
        for(unsigned i=0;i<max_hazard_pointers;++i)
        {
            std::thread::id old_id;
            if(hazard_pointers[i].id.compare_exchange_strong(
                   old_id,std::this_thread::get_id()))
            {
                hp=&hazard_pointers[i];
                break;
            }
        }
        if(!hp)
        {
            throw std::runtime_error("No hazard pointers available");
        }
    }
    std::atomic<void*>& get_pointer()
    {
        return hp->pointer;
    }
    ~hp_owner()
    {
        hp->pointer.store(nullptr);
        hp->id.store(std::thread::id());
    }
};
std::atomic<void*>& get_hazard_pointer_for_current_thread()
{
    thread_local static hp_owner hazard;
    return hazard.get_pointer();
}

template<typename T>
void do_delete(void* p)
{
    delete static_cast<T*>(p);
}
struct data_to_reclaim
{
    void* data;
    std::function<void(void*)> deleter;
    data_to_reclaim* next;
    template<typename T>
    data_to_reclaim(T* p):
        data(p),
        deleter(&do_delete<T>),
        next(0)
    {}
    ~data_to_reclaim()
    {
        deleter(data);
    }
};
std::atomic<data_to_reclaim*> nodes_to_reclaim;
void add_to_reclaim_list(data_to_reclaim* node)
{
    node->next=nodes_to_reclaim.load();
    while(!nodes_to_reclaim.compare_exchange_weak(node->next,node));
}
bool outstanding_hazard_pointers_for(void* data){
  return false;
}
template<typename T>
void reclaim_later(T* data)
{
    add_to_reclaim_list(new data_to_reclaim(data));
}
void delete_nodes_with_no_hazards()
{
    data_to_reclaim* current=nodes_to_reclaim.exchange(nullptr);
    while(current)
    {
        data_to_reclaim* const next=current->next;
        if(!outstanding_hazard_pointers_for(current->data))
        {
            delete current;
        }
        else
        {
            add_to_reclaim_list(current);
        }
        current=next;
    }
}

template<typename T>
class lock_free_stack_with_hazard // garbage collection
{
private:
    struct node
    {
        std::shared_ptr<T> data;
        node* next;
        node(T const& data_):
            data(std::make_shared<T>(data_))
        {}
    };
    std::atomic<node*> head;
public:
    std::shared_ptr<T> pop()
    {
      std::atomic<void*>& hp=get_hazard_pointer_for_current_thread();
      node* old_head=head.load();
      do
      {
          node* temp;
          do
          {
              temp=old_head;
              hp.store(old_head);
              old_head=head.load();
          } while(old_head!=temp);
      }
      while(old_head &&
            !head.compare_exchange_strong(old_head,old_head->next));
      hp.store(nullptr);
      std::shared_ptr<T> res;
      if(old_head)
      {
          res.swap(old_head->data);
          if(outstanding_hazard_pointers_for(old_head))
          {
              reclaim_later(old_head);
          }
          else
          {
              delete old_head;
          }
          delete_nodes_with_no_hazards();
      }
      return res;
    }
};

// Using split reference counter
template<typename T>
class lock_free_stack_rf
{
private:
    struct node;
    struct counted_node_ptr
    {
        int external_count;
        node* ptr;
    };
    struct node
    {
        std::shared_ptr<T> data;
        std::atomic<int> internal_count;
        counted_node_ptr next;
        node(T const& data_):
            data(std::make_shared<T>(data_)),
            internal_count(0)
        {}
    };
    std::atomic<counted_node_ptr> head;
    void increase_head_count(counted_node_ptr& old_counter)
    {
        counted_node_ptr new_counter;
        do
        {
            new_counter=old_counter;
            ++new_counter.external_count;
        }
        while(!head.compare_exchange_strong(
                  old_counter,new_counter,
                  std::memory_order_acquire,
                  std::memory_order_relaxed));
        old_counter.external_count=new_counter.external_count;
    }
public:
    ~lock_free_stack_rf()
    {
        while(pop());
    }
    void push(T const& data)
    {
        counted_node_ptr new_node;
        new_node.ptr=new node(data);
        new_node.external_count=1;
        new_node.ptr->next=head.load(std::memory_order_relaxed);
            while(!head.compare_exchange_weak(
                      new_node.ptr->next,new_node,
                      std::memory_order_release,
                      std::memory_order_relaxed));
    }
    std::shared_ptr<T> pop()
    {
        counted_node_ptr old_head=
            head.load(std::memory_order_relaxed);
        for(;;)
        {
            increase_head_count(old_head);
            node* const ptr=old_head.ptr;
            if(!ptr)
            {
                return std::shared_ptr<T>();
            }
            if(head.compare_exchange_strong(
                   old_head,ptr->next,std::memory_order_relaxed))
            {
                std::shared_ptr<T> res;
                res.swap(ptr->data);
                int const count_increase=old_head.external_count-2;
                if(ptr->internal_count.fetch_add(
                       count_increase,std::memory_order_release)==-count_increase)
                {
                    delete ptr;
                }
                return res;
            }
            else if(ptr->internal_count.fetch_add(
                        -1,std::memory_order_relaxed)==1)
            {
                ptr->internal_count.load(std::memory_order_acquire);
                delete ptr;
            }
        }
    }
};
int main()
{
  return 0;
}
