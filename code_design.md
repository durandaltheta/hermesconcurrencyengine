# Coding Standards
## C++ version:
- must be c++20


## Standard C++:
- if something is available in 'std', use it over external libraries like 'boost'


## Line widths
Line width has a large impact on readability, especially in constrained windowed environments such as IDEs, so wherever it is reasonable make every effort to curtail lines within 80 characters. This includes but is not limited to:
- Breaking single lines into multiple lines
- Simplifying names when possible while maintaining name clarity
- Using references or pointers on local variables that are deeply nested
- Breaking functionality into smaller functions or lambdas


## Naming
- Extremely terse naming should be reserved for:
	- Pointers: var_ptr, vp
	- Counters: c, u, i
	- Temporary variables, ex: variables created in loops
	- Very short functions
	- Templates

- Use 1-3 word names as reasonable. If a word is shortened, ensure the word is at least 3 or 4 characters (whichever is more readable):
	- current_variable
	- my_variable
	- i_am_groot

- Avoid overly complicated names when simpler names will do
	- Avoid: a_highly_specific_variable_name
	- Embrace: specific_variable  

- Do not include type information in the name, as this is both harder to read and a risk in case the actual type changes.


## Auto
The auto keyword often simplifies text, eases code refactoring, and it is easy for the reader to derive type using the right hand value. Therefore:
- Use auto whenever appropriate for variables 


## Getters and Deep Member Access
If a getter function or a deep object access needs to happen more than once, create a reference to the result or copy the value. This improves readability by removing unnecessary text and possibly performance.

- embrace:
```
auto my_val = obj::instance()->getter();
auto& instance = *(obj::instance());
auto& my_val = obj.obj2.obj3.my_val;
```


## Namespaces:
- Do not use "using namespace" keywords in header code.
- When declaring code in a namespace, do not tab the child code. Prefer:
``` 
//outer namespaces are the exception to bracket scope styling, to promote 
//readability when 'layering' namespaces as below
namespace outer_namespace { 
namespace my_space {

my_code_on_the_same_column;

}
}
```


## Whitespace:
- Use 4 spaces, not tabs


## Naming:
- object, functions, variables, and filenames are lowercase with underscores
    - follows c++ std pattern
    - eliminates any behavorial expectations based on camelCase/UpperCamel
    - underscores are always separators; they cannot be confused with letters
- private members are postfixed with '_ ' (ie: member_variable_)
- globals are prefixed with 'g_'
- thread_locals (c++11 thread scoped globals) are prefixed with 'tl_'
- defines are UPPERCASE


## Boolean Logic
When boolean logic is not trivial, store result in a named variable instead of 
directly used in an if, else if, while, or for. This improves readability by 
separating the decision making from the result of the decision, as well as 
describing the algorithm by giving the logic a name:
```
bool we_are_in_x_state = (a && (b || c)) || d;

if(we_are_in_x_state) {
    ...
}
```


## Brackets:
- Always use brackets with if/else clauses to protect against future changes to the code.

- Use the following style for scopes
```
return_type function_name(args...) {
}
```

- and:
```
if(test) {
    // ... lines ...
    // ... lines ...
    // ... lines ...
} else if(test) {
    // ... lines ...
    // ... lines ...
    // ... lines ...
} else {
    // ... lines ...
    // ... lines ...
    // ... lines ...
}
```

- Single line if/else if/else clauses can be optionally compressed like thus:

```
if(val) { single_line_operation; }
else if(val2) { single_line_operation2; }
else { single_line_operation3; }
```


## Comma Separated Lists:
Use the following styles:

- Single line: 
```
{arg1, arg2, arg3};
```

- Multiple line:  
```
    {arg1,
     arg2,
     arg3};
```
- If a function's usage, declaration, or definition is longer than 80 chars,
feel free to break the function's argument list into multiple lines:

```
return_value my_function_is_too_long(type1 arg1,
                                     type2 arg2,
                                     type3 arg3) {
    ...
}
```

## Branching Attributes
Certain performance critical sections of code utilize `[[likely]]` and `[[unlikely]]` attributes to encourage compiler branching optimization. `[[likely]]` should be used to encourage optimization for processing multiple operations over single-shot operations. Examples: assuming multiple coroutines are ready to process, or assuming the coroutine will continue running after suspend.

That is, the benefit of optimizing for short operations is negligible, but the benefit for optimizing for high throughput scenarios is very great.
