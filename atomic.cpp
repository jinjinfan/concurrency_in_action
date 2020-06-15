#include <iostream>
#include <atomic>
using namespace std;

// All atomic types 
/* 1. don't support assignment and copy-construction
   2. the assignment operators they support return values (of the corresponding non-atomic type) 
rather than references. 
*/
// atomic_flag guarantee to be lock free
class spinlock_mutex
{
    std::atomic_flag flag;
public:
    spinlock_mutex():
        flag(ATOMIC_FLAG_INIT)
    {}
    void lock()
    {
        while(flag.test_and_set(std::memory_order_acquire)); // busy wait lock, consume CPU
    }
    void unlock()
    {
        flag.clear(std::memory_order_release);
    }
};

void atomicflag()
{
  // atomic_flag lock free
  std::atomic_flag f = ATOMIC_FLAG_INIT;
  f.clear(std::memory_order_release);
  bool x = f.test_and_set();
  cout << x << endl;
}

void atomicbool()
{
  std::atomic<bool> f{false};
  bool b = f.load(std::memory_order_acquire);
  f.store(true);
  b = f.exchange(false, std::memory_order_acq_rel);
  cout << boolalpha << b << endl;
  cout << boolalpha << f.load(std::memory_order_acquire) << endl;
  std::atomic<bool> ba{false};
  bool expected{true};
  //Compares the contents of the atomic object's contained value with expected:
  // if true, it replaces the contained value with val (like store).
  // if false, it replaces expected with the contained value .
  ba.compare_exchange_weak(expected,true,
      memory_order_acq_rel,memory_order_acquire);
  ba.compare_exchange_weak(expected,true,memory_order_acq_rel);
  cout << boolalpha << "expected: " << expected << endl;
  cout << boolalpha <<"ba: " << ba << endl;
  cout << std::atomic<bool>{}.is_lock_free() << endl;
}

void atomicpointer()
{
  int some_array[5]{1,2,3,4,5};
  std::atomic<int*> p{some_array};
  int* x = p.fetch_add(2);
  cout << *x << endl; // 1
  cout << *p.load() << endl; // 3
  x=(p-=1);
  cout << *x << endl; // 2
  cout << *p.load() << endl; //2
}
//User define type be used in std::atomic
/*
  1. a trivial copy-assignment operator
  2. bitwise comparison like memcmp and memcpy
*/

/*
Table 5.3. The operations available on atomic types

Operation atomic_flag atomic<bool> atomic<T*> atomic<integral-type> atomic<other-type>
test_and_set	Y	 	 	 	 
clear	        Y	 	 	 	 
is_lock_free	 	          Y	Y	Y	Y
load	 	                  Y	Y	Y	Y
store	 	                  Y	Y	Y	Y
exchange	 	              Y	Y	Y	Y
compare_exchange_weak, 
compare_exchange_strong	 	Y	Y	Y	Y
fetch_add, +=	 	 	          Y	Y	 
fetch_sub, -=	 	 	          Y	Y	 
fetch_or, |=	 	 	 	          Y	 
fetch_and, &=	 	 	 	          Y	 
fetch_xor, ^=	 	 	 	          Y	 
++, --	 	 	                Y	Y	 

*/


// atomic shared_ptr
std::shared_ptr<int> p;
void process_data(std::shared_ptr<int> p) {}
void process_global_data()
{
    std::shared_ptr<int> local=std::atomic_load(&p);
    process_data(local);
}
void update_global_data()
{
    std::shared_ptr<int> local(new int);
    std::atomic_store(&p,local);
}

#include <vector>
#include <atomic>
#include <thread>
#include <chrono>
std::vector<int> datavec;
std::atomic<bool> data_ready{false};
void reader_thread()
{
  while(!data_ready.load())
  {
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  std::cout<<"The answer="<<datavec[0]<<"\n";
}

void writer_thread()
{
    datavec.push_back(42);
    data_ready=true;
}

void synchronize_with()
{
  std::thread read{reader_thread};
  std::thread write{writer_thread};
  write.detach();
  read.join();
}

// Sequentially consistent ordering
/*
the most straightforward and intuitive ordering, but it’s also the most expensive memory 
ordering because it requires global synchronization between all threads. 
On a multiprocessor system this may require extensive and time-consuming communication between processors.
*/
#include <assert.h>
std::atomic<bool> x,y;
std::atomic<int> z;
void write_x()
{
    x.store(true,std::memory_order_seq_cst);
}
void write_y()
{
    y.store(true,std::memory_order_seq_cst);
}
void read_x_then_y()
{
    while(!x.load(std::memory_order_seq_cst));
    if(y.load(std::memory_order_seq_cst))
        ++z;
}
void read_y_then_x()
{
    while(!y.load(std::memory_order_seq_cst));
    if(x.load(std::memory_order_seq_cst))
        ++z;
}

void seq_cst()
{
    x=false;
    y=false;
    z=0;
    std::thread a(write_x);
    std::thread b(write_y);
    std::thread c(read_x_then_y);
    // std::thread d(read_y_then_x);
    a.join();
    b.join();
    c.join();
    // d.join();
    cout << z << endl;
    // assert(z.load()!=0);
}

// Non-sequentially consistent memory orderings
// threads don’t have to agree on the order of events.

// memory_order_relaxed
void write_x_then_y_relax()
{
    x.store(true,std::memory_order_relaxed);
    y.store(true,std::memory_order_relaxed);
}

void read_y_then_x_relax()
{
    while(!y.load(std::memory_order_relaxed));
    if(x.load(std::memory_order_relaxed))
        ++z;
}

void relaxed()
{
    x=false;
    y=false;
    z=0;
    std::thread a(write_x_then_y_relax);
    std::thread b(read_y_then_x_relax);
    a.join();
    b.join();
    cout << z << endl;
    assert(z.load()!=0);
}

//Acquire-release ordering
/*
Synchronization is pairwise between the thread that does the release and the thread that does the acquire.
 A release operation synchronizes-with an acquire operation that reads the value written.
  This means that different threads can still see different orderings, but these orderings are restricted.
*/
void write_x_then_y_acq_rel()
{
    x.store(true,std::memory_order_relaxed);
    y.store(true,std::memory_order_release);
}

void read_y_then_x_acq_rel()
{
    while(!y.load(std::memory_order_acquire));
    if(x.load(std::memory_order_relaxed))
        ++z;
}

void acquirerelease()
{
    x=false;
    y=false;
    z=0;
    std::thread a(write_x_then_y_acq_rel);
    std::thread b(read_y_then_x_acq_rel);
    a.join();
    b.join();
    cout << z << endl;
    assert(z.load()!=0);
}

// transitive acquire-release
std::atomic<int> vecdata[5];
std::atomic<bool> sync1(false),sync2(false);

void thread_1()
{
    vecdata[0].store(42,std::memory_order_relaxed);
    vecdata[1].store(97,std::memory_order_relaxed);
    vecdata[2].store(17,std::memory_order_relaxed);
    vecdata[3].store(-141,std::memory_order_relaxed);
    vecdata[4].store(2003,std::memory_order_relaxed);
    sync1.store(true,std::memory_order_release);
}

void thread_2()
{
    while(!sync1.load(std::memory_order_acquire));
    sync2.store(std::memory_order_release);
}

void thread_3()
{
    while(!sync2.load(std::memory_order_acquire));
    assert(vecdata[0].load(std::memory_order_relaxed)==42);
    assert(vecdata[1].load(std::memory_order_relaxed)==97);
    assert(vecdata[2].load(std::memory_order_relaxed)==17);
    assert(vecdata[3].load(std::memory_order_relaxed)==-141);
    assert(vecdata[4].load(std::memory_order_relaxed)==2003);
}

void acquirereleasetransitive()
{
    std::thread t1(thread_1);
    std::thread t2(thread_2);
    std::thread t3(thread_3);
    t1.join();
    t2.join();
    t3.join();
}

// Fence:memory barrier
void write_x_then_y_fence()
{
    x.store(true,std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    y.store(true,std::memory_order_relaxed);
}

void read_y_then_x_fence()
{
    while(!y.load(std::memory_order_relaxed));
    std::atomic_thread_fence(std::memory_order_acquire);
    if(x.load(std::memory_order_relaxed))
        ++z;
}

void fencesmemory()
{
    x=false;
    y=false;
    z=0;
    std::thread a(write_x_then_y_fence);
    std::thread b(read_y_then_x_fence);
    a.join();
    b.join();
    assert(z.load()!=0);
}
int main()
{
  // atomicbool();
  // atomicpointer();
  // synchronize_with();
  // seq_cst();
  // relaxed();
  // acquirerelease();
  // acquirereleasetransitive();
  fencesmemory();
  return 0;
}
