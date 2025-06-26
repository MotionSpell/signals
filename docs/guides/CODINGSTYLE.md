# clang-format {#codingstyle}
\page codingstyle Coding Style
\ingroup guides

Clang-Format is a code beautifier tool. Code should be compliant with the .clang-format file present at the root folder of this repository.


# Example

```cpp
namespace OtherNamespace {
class MyForwardDeclaredType;
}

namespace Signals {
/**
 * A class that does cool stuff.
 * Some general comments can be added if needed, and possibly sample code for public headers.
 */
template <typename T>
class MyClass {
public:
    typedef T value_type;
    enum MyEnum {EnumTypeA = 0, EnumTypeB = 1}; 
    
    /**
    * Public methods come first. No need to comment if not useful.
    */
    explicit MyClass(const std::string &s);
    ~MyClass();

    /**
    * Document member function, but do not go overboard.
    */
    void myMemberFunction() const;

    /*
    * NEVER use public variables; except static const POD.
    */
    static const double myConst;

protected:
    int myVar; //small comments like this

private:
    /**
    * Large comments like this
    */
    std::vector<T> myVect;
};

const double MyClass::myConst = 42.0;

template <typename T>;
MyClass<T>::MyClass(const std::string &s)
: myVar(3) {
    if (s.size()) {
        for (int i = 0; i < s.size(); ++i) {
            // ...
        }
    } else if (myVar) {
        // ...
    } else {
        // ...
    }
}
}
```

# Indentation and spacing

* Tabs. Additional spaces on defines, large comments, or variable names aligning.
* public, protected, private are on the first column.
* Opening brace on the same line as function/control structure/initializer list.
* else goes on the same line as the closing brace.
* Closing brace on new line.
* Empty loops like this: while (...) {
                         }
* The declaring template keyword goes on its own line.
* Equal signs, and semicolumns in for loops, have a space on each side.
* There is always a space after a comma.
* Pointer is on the right: A *a; const A *a;
* Pointer on return type (and other constness) can be on the right: A* get(); const A* const;


# Case

* Type names, template parameters and enum constants use TypeCase
* (static const) (member) variable, and (member) function names use camelCase, not under_scores.


# Order

* Public typedefs
* Public enums
* Public methods
* Public static const POD variables. const public variables sometimes OK.
* Protected methods
* Protected variables
* Private methods
* Private variables
* Constness
* Be (const) correct for arguments.
* Be (const) correct for member functions.
* Use either const A* const or A const* const.


# Headers

* They are named *.hpp for C++, *.h for C.
* Always put guards in header files: #pragma once
* Include what you use, and only what you use.
* Only forward declare if you only need a reference or pointer to type.


# Lines

* No line length limit. Split as few as possible. Try to be consistent.
* Avoid trailing whitespace.
* One statement per line.
* Unix '\n' ending on the repository.


# Namespaces

* In header files, always use fully qualified name for standard library (e.g.using namespace std; is only allowed in implementation files).
* No indent in namespaces.


# Templates

* If possible, put the code in a cpp file, and instantiate them explicitely.


# Portability

* Assume little endian.
* Don't assume type sizes (except char and unsigned char). Use for portable fixed size types.
* The code should be free of platform dependancy, except at the top of each file.
* Wrap up platform-dependant code into common APIs.


# Performance

* This applies mostly to the input/output/core code.
* Avoid virtual calls in perf-critical parts of the code. In this case you may use static virtual inheritance (template powa).
* Always pass objects per (const) reference (an exception for very lightweight objects such as iterators, but you should document that).
* Allocate small objects on the stack.
* Recycle objects that are on the heap. You should see new mostly in constructors and delete mostly in destructors.
* Do not return large objects. Take a reference/pointer as output.


# Misc

* Use assert for impossible conditions. Do not put statements with side effects in asserts.
* Do not use exit() except on tests.
* Use TODO and FIXME in comments.
* Variable names should be descriptive, make them long rather than short.
* Experimental code goes between #if 0 #endif. Dead code should be removed (we'll get it back from git if necessary).
* Avoid global variables.
* Inner classes are OK. Don't abuse them.
* Use functors, not function pointers.
* Do not use the comma operator, it's mostly confusing.
* Do not overload operators too much.
* Undefined behaviour is never OK, even if its works (for the time being).
* Generally avoid gotos.
* Be warning free (in -Wall mode).
