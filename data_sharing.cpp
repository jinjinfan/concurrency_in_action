#include <iostream>
#include <exception>
#include <stack>
#include <mutex>
#include <memory>
using namespace std;

//mutex(mutual exclusion)
/*std::mutex some_mutex;
std::lock_guard<std::mutex> lg{some_mutex}; // locks the supplied mutex on construction and unlocks it on destruction
std::lock_guard lg{some_mutex}; //class template argument deduction in c++17
*/
// Thread safe stack to avoid data race
//https://en.wikipedia.org/wiki/Software_transactional_memory
struct empty_stack:std::exception
{
  const char* what() const throw()
  // throw() Non-throwing dynamic exception specification
  // Lists the exceptions that a function might directly or indirectly throw.
  {
    return "empty stack";
  }
};

template <typename T>
class threadsafe_stack
{
  std::stack<T> data;
  mutable std::mutex m;
public:
  threadsafe_stack() {}
  threadsafe_stack(const threadsafe_stack& other)
  {
    std::lock_guard<mutex> lock(other.m);
    data = other.data;
  }
  threadsafe_stack& operator=(const threadsafe_stack&) = delete;

  void push(T new_value)
  {
    std::lock_guard<mutex> lock(m);
    data.push(new_value);
  }
  std::shared_ptr<T> pop()
  {
    std::lock_guard<mutex> lock(m);
    if (data.empty()) throw empty_stack();
    std::shared_ptr<T> const res(make_shared<T>(data.top()));
    data.pop();
    return res;
  }
  void pop(T& value)
  {
    std::lock_guard<mutex> lock(m);
    if (data.empty()) throw empty_stack();
    value = data.top();
    data.pop();
  }
  bool empty() const
  {
    std::lock_guard<mutex> lock(m);
    return data.empty();
  }
};

void threadsafestack()
{
  threadsafe_stack<int> si;
  si.push(5);
  auto a = si.pop();
  cout << *a << endl;
  if (!si.empty())
  {
    int x;
    si.pop(x);
  }
}

class some_big_object
{};

void swap(some_big_object& lhs,some_big_object& rhs)
{}

// resolve deadlock
/*
1. avoid nested lock
2. avoid calling user-supplied code while holding a lock
3. acquire locks in a fix order
4. use a lock hierarchy
*/
class X
{
private:
    some_big_object some_detail;
    mutable std::mutex m;
public:
    X(some_big_object const& sd):some_detail(sd){}

    friend void swap(X& lhs, X& rhs)
    {
        if(&lhs==&rhs)
            return;
        std::lock(lhs.m, rhs.m);
        std::lock_guard<std::mutex> lock_a(lhs.m,std::adopt_lock);
        std::lock_guard<std::mutex> lock_b(rhs.m,std::adopt_lock);
        // adopt the ownership of the existing lock on the mutex rather than attempt to lock the mutex in the constructor.
        swap(lhs.some_detail,rhs.some_detail);
    }
    friend void swap2(X& lhs, X& rhs)
    {
      if (&lhs == &rhs) return;
      std::scoped_lock guard(lhs.m, rhs.m);
      swap(lhs.some_detail,rhs.some_detail);
    }
    friend void swap3(X& lhs, X& rhs)
    {
      if (&lhs == &rhs) return;
      //std::unique_lock instance doesn’t always own the mutex that it’s associated with
      /*
      std::unique_lock takes more space and is slightly slower to use than std::lock_guard. 
      The flexibility of allowing an std::unique_lock instance not to own the mutex comes at a price: 
      this information has to be stored, and it has to be updated.
      This flag can be queried by calling the owns_lock() member function to inducate if instance owns the mutex
      * when to use unique_lock
      * One example is deferred locking, as you’ve already seen; 
      * another case is where the ownership of the lock needs to be transferred from one scope to another.
      *
      */
      std::unique_lock<std::mutex> lock_a(lhs.m,std::defer_lock);
      std::unique_lock<std::mutex> lock_b(rhs.m,std::defer_lock);
      // the mutex should remain unlocked on construction
      std::lock(lock_a,lock_b);
    }
};

void deadlock()
{
  X x1{some_big_object{}};
  X x2{some_big_object{}};
  swap2(x1, x2);
}

#include <stdexcept>
#include <climits>
class hierarchical_mutex
{
    std::mutex internal_mutex;
    unsigned long const hierarchy_value;
    unsigned long previous_hierarchy_value;
    static thread_local unsigned long this_thread_hierarchy_value;

    void check_for_hierarchy_violation()
    {
        if(this_thread_hierarchy_value <= hierarchy_value)
        {
            throw std::logic_error("mutex hierarchy violated");
        }
    }
    void update_hierarchy_value()
    {
        previous_hierarchy_value=this_thread_hierarchy_value;
        this_thread_hierarchy_value=hierarchy_value;
    }
public:
    explicit hierarchical_mutex(unsigned long value):
        hierarchy_value(value),
        previous_hierarchy_value(0)
    {}
    void lock()
    {
        check_for_hierarchy_violation();
        internal_mutex.lock();
        update_hierarchy_value();
    }
    void unlock()
    {
        this_thread_hierarchy_value=previous_hierarchy_value;
        internal_mutex.unlock();
    }
    bool try_lock()
    {
        check_for_hierarchy_violation();
        if(!internal_mutex.try_lock())
            return false;
        update_hierarchy_value();
        return true;
    }
};
thread_local unsigned long
    hierarchical_mutex::this_thread_hierarchy_value(ULONG_MAX);
    // thread_local:
    // The storage for the object is allocated when the thread begins and deallocated when the thread ends. 
    // Each thread has its own instance of the object.   

void mutexhierarche()
{
    hierarchical_mutex m1(42);
    hierarchical_mutex m2(2000);
}

// double checked lock
/*
Unfortunately, this pattern is infamous for a reason: it has the potential for nasty race conditions, because the read outside 
the lock 1, isn’t synchronized with the write done by another thread inside the lock 3. 
This creates a race condition that covers not only the pointer itself but also the object pointed to; 
even if a thread sees the pointer written by another thread, it might not see the newly created instance of some_resource, 
*/
class some_resource
{
  public:
    void do_something() {}
};
std::shared_ptr<some_resource> resource_ptr;
std::mutex resource_mutex;
void undefined_behavior_foo()
{
    if(!resource_ptr)
    {
        std::lock_guard lk{resource_mutex};
        if(!resource_ptr)
        {
          resource_ptr.reset(new some_resource);
        }
    }
    resource_ptr->do_something();
}
// call_once to initialize once the resource
std::once_flag resource_flag;
void init_resource()
{
    resource_ptr.reset(new some_resource);
}
void foo()
{
    std::call_once(resource_flag, init_resource);
    resource_ptr->do_something();
}

// call once lazy_initialization
struct connection_info
{};

struct data_packet
{};

struct connection_handle
{
    void send_data(data_packet const&)
    {}
    data_packet receive_data()
    {
        return data_packet();
    }
};
struct remote_connection_manager
{
    connection_handle open(connection_info const&)
    {
        return connection_handle();
    }
} connection_manager;

class connectionManager
{
private:
  connection_info connection_details;
  connection_handle connection;
  std::once_flag connection_init_flag;
  void open_connection()
  {
    connection = connection_manager.open(connection_details);
  }
public:
  connectionManager(connection_info const& connection_details_)
  :connection_details(connection_details_) {}
  void send_data(data_packet const& data)
  {
    std::call_once(connection_init_flag, &connectionManager::open_connection, this);
    connection.send_data(data);
  }
  data_packet receive_data()
  {
      std::call_once(connection_init_flag,&connectionManager::open_connection,this);
      return connection.receive_data();
  }
};
//std::shared_mutex
/*
The only constraint is that if any thread has a shared lock, a thread that tries to 
acquire an exclusive lock will block until all other threads have relinquished their locks,
and likewise if any thread has an exclusive lock, no other thread may acquire a shared or 
exclusive lock until the first thread has relinquished its lock.
*/
#include <map>
#include <shared_mutex>
class dns_entry
{};

class dns_cache
{
    std::map<std::string,dns_entry> entries;
    std::shared_mutex entry_mutex;
public:
    dns_entry find_entry(std::string const& domain)
    {
        std::shared_lock<std::shared_mutex> lk(entry_mutex); // read_only lock
        std::map<std::string,dns_entry>::const_iterator const it=
            entries.find(domain);
        return (it==entries.end())?dns_entry():it->second;
    }
    void update_or_add_entry(std::string const& domain,
                             dns_entry const& dns_details)
    {
        std::lock_guard<std::shared_mutex> lk(entry_mutex); // exclusive lock
        entries[domain]=dns_details;
    }
};

int main ()
{
  // threadsafestack();
  // deadlock();
  mutexhierarche();
  return 0;
}
