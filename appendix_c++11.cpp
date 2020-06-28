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
1. Defaulted functions
Objects with trivial copy constructors, trivial copy assignment operators, and trivial destructors can be copied with memcpy or memmove.
Literal types used for constexpr functions (see section A.4) must have a trivial constructor, copy constructor, and destructor.
Classes with a trivial default constructor, copy constructor, copy assignment operator, and destructor can be used in a union with a user-defined constructor and destructor.
Classes with trivial copy assignment operators can be used with the std::atomic<> class template (see section 5.2.6) in order to provide a value of that type with atomic operations.
*/
/*
The second difference between classes with compiler-generated functions and user-supplied equivalents 
is that a class with no user-supplied constructors can be an aggregate and thus can be initialized 
with an aggregate initializer:
*/
struct aggregate
{
    aggregate() = default;
    aggregate(aggregate const&) = default;
    int a;
    double b;
};
aggregate x={42,3.141};

/*
2. const expression
The key benefit of constant expressions and constexpr functions involving user-defined types is 
that objects of a literal type initialized with a constant expression are statically initialized,
and so their initialization is free from race conditions and initialization order issues:
*/

struct my_aggregate
{
    int a;
    int b;
};
static my_aggregate ma1={42,123};
// Static initialization like this can be used to avoid order-of-initialization problems and race conditions.
int dummy=257;
static my_aggregate ma2={dummy,dummy};

/*
The constexpr keyword is primarily a function modifier. If the parameter and return type of a function meet
certain requirements and the body is sufficiently simple, a function can be declared constexpr, in which 
case it can be used in constant expressions
*/
constexpr int square(int x)
{
    return x*x;
}
int array[square(5)];

/*
Literal types
*/
/*
For a class type to be classified as a literal type, the following must all be true:
It must have a trivial copy constructor.
It must have a trivial destructor.
All non-static data members and base classes must be trivial types.
It must have either a trivial default constructor or a constexpr constructor other than the copy constructor.
*/

/*
3. Lambda
*/
//generalized captures
//can capture the results of expressions, rather than a direct copy of or reference to a local variable. 
// Most commonly this can be used to capture move-only types by moving them, rather than having to capture
// by reference; 
std::future<int> spawn_async_task(){
    std::promise<int> p;
    auto f=p.get_future();
    std::thread t([p=std::move(p)](){ p.set_value(find_the_answer());});
    t.detach();
    return f;
}

/*
4. Variadic template
*/
template<typename ReturnType, typename ... Args>
//The variadic parameter Args is called a parameter pack, 
// and the use of Args... is called a pack expansion.
class packaged_task<ReturnType(Args...)>

template<typename ... Args>
unsigned count_args(Args ... args)
{
    return sizeof... (Args); // the number of elements in the parameter pack
}

int main()
{
    int offset=42;
    std::function<int(int)> offset_a=[&](int j){return offset+j;};
    offset=123;
    std::function<int(int)> offset_b=[&](int j){return offset+j;};
    std::cout<<offset_a(12)<<","<<offset_b(12)<<std::endl;
    offset=99;
    std::cout<<offset_a(12)<<","<<offset_b(12)<<std::endl;
    return 0;
}
