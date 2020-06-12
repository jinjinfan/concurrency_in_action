#include <iostream>
#include <list>
#include <algorithm>
#include <chrono>
#include <future>
using namespace std;

// Function programming quick sort
template <typename T>
std::list<T> sequential_quick_sort(std::list<T> input)
{
  if (input.empty())
  {
    return input;
  }
  std::list<T> result;
  result.splice(result.begin(), input, input.begin());
  T const& pivot = *result.begin();
  auto divide_point = std::partition(input.begin(), input.end(),
                    [&](T const& t){return t < pivot;});
  std::list<T> lower_part;
  lower_part.splice(lower_part.end(), input, input.begin(), divide_point);
  auto new_lower(sequential_quick_sort(std::move(lower_part)));
  auto new_higher(sequential_quick_sort(std::move(input)));
  result.splice(result.end(), new_higher);
  result.splice(result.begin(), new_lower);
  return result;
}

template<typename T>
std::list<T> parallel_quick_sort(std::list<T> input)
{
  if (input.empty())
  {
    return input;
  }
  std::list<T> result;
  result.splice(result.begin(), input, input.begin());
  T const& pivot = *result.begin();
  auto divide_point = std::partition(input.begin(), input.end(),
                    [&](T const& t){return t < pivot;});
  std::list<T> lower_part;
  lower_part.splice(lower_part.end(), input, input.begin(), divide_point);
  std::future<std::list<T>> new_lower{
    std::async(&parallel_quick_sort<T>, std::move(lower_part))
  };
  auto new_higher(parallel_quick_sort(std::move(input)));
  result.splice(result.end(), new_higher);
  result.splice(result.begin(), new_lower.get());
  return result;
}

template<typename F, typename A>
std::future<typename std::result_of<F(A&&)>::type>
spawn_task(F&& f, A&& a)
{
  typedef typename std::result_of<F(A&&)>::type result_type;
  std::packaged_task<result_type(A&&)> task(std::move(f));
  std::future<result_type> res(task.get_future());
  std::thread t{std::move(task), std::move(a)};
  t.detach();
  return res;
}

void fp_sort()
{
  std::list<int> ab{10,9,8,7,6,5,4,3,2,1};
  auto start=std::chrono::high_resolution_clock::now();
  auto result = parallel_quick_sort(ab);
  auto stop=std::chrono::high_resolution_clock::now();
  for (int a : result)
    cout << a << endl;
  std::cout<<"sort took "
  << std::chrono::duration<double, std::ratio<1,1>>(stop-start).count()
  <<" seconds"<<std::endl;
}

//CSP (Communicating Sequential Processes),
// Message passing interface
int main()
{
  fp_sort();
  return 0;
}
