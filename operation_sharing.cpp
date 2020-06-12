#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <condition_variable>
#include <chrono>
using namespace std;

void thread_sleep()
{
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

bool more_data_to_prepare()
{
    return true;
}

struct data_chunk
{
  int test;
  data_chunk(int a): test(a){}
};

data_chunk prepare_data()
{
    return data_chunk(10);
}

void process(data_chunk&)
{}

bool is_last_chunk(data_chunk&)
{
    return true;
}

std::mutex mut;
std::condition_variable data_cond;
// a condition variable may check the supplied condition any number of 
// times; but it always does so with the mutex locked and will 
// return immediately if (and only if) the function provided to test 
// the condition returns true. 

template <typename T>
class threadsafe_queue
{
  mutable std::mutex mut;
  // Define as mutable here
  /*
  Even though empty() is a const member function, and the other 
  parameter to the copy constructor is a const reference, 
  other threads may have non-const references to the object, 
  and may be calling mutating member functions, 
  so you still need to lock the mutex. 
  Since locking a mutex is a mutating operation, 
  the mutex object must be marked mutable 1 so it 
  can be locked in empty() and in the copy constructor.
  */

  std::queue<T> data_queue;
  std::condition_variable data_cond;
public:
  threadsafe_queue() {}
  threadsafe_queue(threadsafe_queue const& other)
  {
    std::lock_guard<std::mutex> lk(other.mut);
    data_queue = other.data_queue;
  }
  threadsafe_queue& operator=(const threadsafe_queue& other) = delete;
  void push(T new_value)
  {
    std::lock_guard<std::mutex> lk(mut);
    data_queue.push(new_value);
    data_cond.notify_one();
  }
  void wait_and_pop(T& value)
  {
    std::unique_lock<std::mutex> lk(mut);
    data_cond.wait(lk, [this](){return !data_queue.empty();});
    value = data_queue.front();
    data_queue.pop();
  }
  shared_ptr<T> wait_and_pop()
  {
    std::unique_lock<std::mutex> lk(mut);
    data_cond.wait(lk, [this](){return !data_queue.empty();});
    shared_ptr<T> res(make_shared<T>(data_queue.front()));
    data_queue.pop();
    return res;
  }
  bool try_pop(T& value)
  {
    std::lock_guard<std::mutex> lk(mut);
    if(data_queue.empty())
      return false;
    value = data_queue.front();
    data_queue.pop();
    return true;
  }
  shared_ptr<T> try_pop()
  {
    std::lock_guard<std::mutex> lk(mut);
    if(data_queue.empty())
      return shared_ptr<T>();
    shared_ptr<T> res{make_shared(data_queue.front())};
    data_queue.pop();
    return res;
  }
  bool empty() const
  {
      std::lock_guard<std::mutex> lk(mut);
      return data_queue.empty();
  }
};

threadsafe_queue<data_chunk> data_queue;
void data_preparation_thread()
{
    while(more_data_to_prepare())
    {
        data_chunk const data=prepare_data();
        data_queue.push(data);
    }
}

void data_processing_thread()
{
    while(true)
    {
        data_chunk data{10};
        data_queue.wait_and_pop(data);
        cout << "data poped from queue will be " << data.test << endl;
        process(data);
        if(is_last_chunk(data))
            break;
    }
}

void conditionvariable()
{
    std::thread t1(data_preparation_thread);
    std::thread t2(data_processing_thread);
    t1.join();
    t2.join();
}

//WAITING FOR ONE-OFF EVENTS WITH FUTURES
// std::async()
//This parameter is of the type std::launch, 
// std::launch::deferred: the function call is to be deferred until either 
//              wait() or get() is called on the future
// std::launch::async: the function must be run on its own thread
// std::launch::deferred | std::launch::async:the implementation may choose. 
// This last option is the default. If the function call is deferred, 
// it may never run.
/*
auto f6=std::async(std::launch::async,Y(),1.2);
auto f7=std::async(std::launch::deferred,baz,std::ref(x));
auto f8=std::async(
   std::launch::deferred | std::launch::async,
   baz,std::ref(x));
auto f9=std::async(baz,std::ref(x));
f7.wait();
*/
/* 1. use std::async() to create future object*/
#include <future>
int find_the_answer_to_ltuae()
{
    return 42;
}

void do_other_stuff()
{}

void withfuture()
{
    std::future<int> the_answer=std::async(find_the_answer_to_ltuae);
    do_other_stuff();
    if( the_answer.wait_for(std::chrono::milliseconds(35)) == std::future_status::ready)
      std::cout<<"The answer is "<<the_answer.get()<<std::endl; 
    // the thread blocks until future is ready
}

/*
Passing tasks between threads
*/
//packaged_task
//wraps a callable element and allows its result to be 
//retrieved asynchronously.
/* 2. use std::packaged_task to create future object
    with member function get_future*/
template <>
class packaged_task<std::string(std::vector<char>*, int)>
{
public:
    template<typename Callable>
    explicit packaged_task(Callable&& f);
    std::future<std::string> get_future(); // same as return type
    void operator()(std::vector<char>*,int); // same as parameter type
};

std::mutex m;
std::deque<std::packaged_task<void()> > tasks;
bool gui_shutdown_message_received() {}
void get_and_process_gui_message() {}

void gui_thread()
{
  while(!gui_shutdown_message_received())
  {
    get_and_process_gui_message();
    std::packaged_task<void()> task;
    {
      std::lock_guard<std::mutex> lk(m);
      if (tasks.empty()) continue;
      task = std::move(tasks.front());
      tasks.pop_front();
    }
    task();
  }
}

template <typename Func>
std::future<void> post_task_for_gui_thread(Func f)
{
    std::packaged_task<void()> task(f);
    std::future<void> res=task.get_future();
    std::lock_guard<std::mutex> lk(m);
    tasks.push_back(std::move(task));
    return res;
}

/*3. use std::promise to create future object
    with member function get_future()
 Use set_value() to make associated future ready
 If you destroy the std::promise without setting a value, an exception is stored instead.
 */

// save exceptions
double square_root(double a) { return 0; }
std::future<double> f=std::async(square_root,-1);
double y=f.get(); 
// exception will be passed to future object, same as std::packaged_task
// destroy the std::promise or std::packaged_task associated with the future 
// the destructor of std::promise or std::packaged_task will 
// store a std::future_error exception with an error code of std::future_errc::broken_promise
double calculate_value() {return 2;}
std::promise<double> some_promise;
void promise_excep()
{
  try
  {
      some_promise.set_value(calculate_value());
  }
  catch(...)
  {
      some_promise.set_exception(std::current_exception());
      some_promise.set_exception(make_exception_ptr(std::logic_error("foo")));
  }
}

void sharedfuture()
{
  std::promise<int> p;
  p.set_value(10);
  std::shared_future<int> sf{p.get_future()};
  auto sf1 = sf;
  cout << sf.get() << endl;
  cout << sf1.get() << endl;
}

void timetest()
{
  using std::chrono::system_clock;
  system_clock::time_point today = system_clock::now();
  std::time_t tt;
  tt = system_clock::to_time_t ( today );
  std::cout << "today is: " << ctime(&tt);
  //predefined literal suffix operators for durations
  using namespace std::chrono_literals;
  auto one_day=24h;
  auto half_an_hour=30min;
  auto max_time_between_messages=30ms;
  cout << std::chrono::milliseconds(1234).count() << endl;
  // Calculate duration
  auto start=std::chrono::high_resolution_clock::now();
  [](){cout << "" << endl;}();
  auto stop=std::chrono::high_resolution_clock::now();
  std::cout<<"do_something() took "
  << std::chrono::duration<double, std::ratio<1,1>>(stop-start).count()
  <<" seconds"<<std::endl;
  //conditional variable wait until
  auto const timeout= std::chrono::steady_clock::now()+ std::chrono::milliseconds(500);
  condition_variable cv;
  if(cv.wait_until(lk,timeout)==std::cv_status::timeout) {}
}
int main ()
{
  // conditionvariable();
  // withfuture();
  // sharedfuture();
  timetest();
  return 0;
}
