#include <thread>
#include <vector>
#include <future>
#include <list>
#include <memory>
#include <stack>
#include <algorithm>
#include <iostream>
#include <new>
#include <numeric>

/*
The key things to bear in mind when designing your data structures for 
multithreaded performance are
1. contention
2. false sharing
3. data proximity
*/

/*
1. Data contention and cache ping-pong
*/
/*
std::atomic<unsigned long> counter(0);
void processing_loop()
{
    while(counter.fetch_add(1,std::memory_order_relaxed)<100000000)
    {
        do_something();
    }
}
*/

//This situation is called high contention. If the processors rarely have to 
// wait for each other, you have low contention.
// In a loop like this one, the data for counter will be passed back and 
// forth between the caches many times. This is called cache ping-pong, and it 
// can seriously impact the performance of the application. If a processor 
// stalls because it has to wait for a cache transfer, it can’t do any work 
// in the meantime, even if there are other threads waiting that could do useful 
// work, so this is bad news for the whole application.

/*
The effects of contention with mutexes are usually different from the effects 
of contention with atomic operations for the simple reason that the use of a 
mutex naturally serializes threads at the operating system level rather than 
at the processor level. If you have enough threads ready to run, the operating 
system can schedule another thread to run while one thread is waiting for 
the mutex, whereas a processor stall prevents any threads from running on 
that processor. But it will still impact the performance of those threads 
that are competing for the mutex; they can only run one at a time, after all.
*/

/*
2. False Sharing
*/
/*
Processor caches don’t generally deal in individual memory locations; instead,
they deal in blocks of memory called cache lines. These blocks of memory are 
typically 32 or 64 bytes in size, but the exact details depend on the particular
 processor model being used.
Suppose you have an array of int values and a set of threads that each access 
their own entry in the array but do so repeatedly, including updates. 
Because an int is typically much smaller than a cache line, quite a few of 
those array entries will be in the same cache line. Consequently, even though
 each thread only accesses its own array entry, the cache hardware still has 
 to play cache ping-pong. Every time the thread accessing entry 0 needs to 
 update the value, ownership of the cache line needs to be transferred to 
 the processor running that thread, only to be transferred to the cache for 
 the processor running the thread for entry 1 when that thread needs to update
  its data item. The cache line is shared, even though none of the data is,
   hence the term false sharing. 

The solution here is to structure the data so that data items to be accessed 
by the same thread are close together in memory (and thus more likely to be 
in the same cache line), whereas those that are to be accessed by separate threads
 are far apart in memory and thus more likely to be in separate cache lines. 
*/

/*
3. Exception safety
*/

/*
4. Scalable
*/
/*
scalable if the performance (whether in terms of reduced speed of execution or 
increased throughput) increases as more processing cores are added to the system.
Ideally, the performance increase is linear, so a system with 100 processors
performs 100 times better than a system with one processor.

Scalability is about reducing the time it takes to perform an action or increasing
the amount of data that can be processed in a given time as more processors are
added. 

*/

class join_threads
{
    std::vector<std::thread>& threads;
public:
    explicit join_threads(std::vector<std::thread>& threads_):
        threads(threads_)
    {}
    ~join_threads()
    {
        for(unsigned long i=0;i<threads.size();++i)
        {
            if(threads[i].joinable())
                threads[i].join();
        }
    }
};

template<typename Iterator,typename Func>
void parallel_for_each(Iterator first,Iterator last,Func f)
{
    unsigned long const length=std::distance(first,last);

    if(!length)
        return;

    unsigned long const min_per_thread=25;
    unsigned long const max_threads=
        (length+min_per_thread-1)/min_per_thread;

    unsigned long const hardware_threads=
        std::thread::hardware_concurrency();

    unsigned long const num_threads=
        std::min(hardware_threads!=0?hardware_threads:2,max_threads);

    unsigned long const block_size=length/num_threads;

    std::vector<std::future<void> > futures(num_threads-1);
    std::vector<std::thread> threads(num_threads-1);
    join_threads joiner(threads);

    Iterator block_start=first;
    for(unsigned long i=0;i<(num_threads-1);++i)
    {
        Iterator block_end=block_start;
        std::advance(block_end,block_size);
        std::packaged_task<void(void)> task(
          [=]()
          {
            std::for_each(block_start,block_end, f);
          }
        );
        futures[i]=task.get_future();
        threads[i]=std::thread(std::move(task));
        block_start=block_end;
    }
    std::for_each(block_start,last,f);
    for(unsigned long i=0;i<(num_threads-1);++i)
    {
        futures[i].get();
    }
}

void parallel_foreach()
{
  int max_num = 100;
	std::vector<int> a(max_num);
	std::iota(a.begin(), a.end(), 1);
  parallel_for_each(a.begin(), a.end(), [](auto p)
  {
    std::cout << p << std::endl;
  });
}

template<typename Iterator,typename Func>
void parallel_for_each_async(Iterator first,Iterator last,Func f)
{
    unsigned long const length=std::distance(first,last);

    if(!length)
        return;

    unsigned long const min_per_thread=25;

    if(length<(2*min_per_thread))
    {
        std::for_each(first,last,f);
    }
    else
    {
        Iterator const mid_point=first+length/2;
        std::future<void> first_half=
            std::async(&parallel_for_each_async<Iterator, Func>,
                        first, mid_point,f);
        parallel_for_each_async(mid_point,last,f);
        first_half.get();
    }
}

void parallel_foreach_async()
{
  int max_num = 100;
	std::vector<int> vec(max_num);
	std::iota(vec.begin(), vec.end(), 1);

  parallel_for_each_async(vec.begin(), vec.end(), [](auto p)
  {
    std::cout << p << std::endl;
  });
}

int main()
{
  std::cout << std::thread::hardware_concurrency() << std::endl;
  // std::cout << std::hardware_destructive_interference_size << std::endl;
  // std::cout << std::hardware_constructive_interference_size << std::endl;
  parallel_foreach();
  // parallel_foreach_async();

  
   //specifies the maximum number of consecutive bytes that may be subject 
   // to false sharing for the current compilation target

  return 0;
}
