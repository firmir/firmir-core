# `sample.hpp`

## class `Greeter`

Produces greeting messages.

```cpp
class Greeter {
```

### `Greeter::Greeter`

Constructs a greeter with the given name.
- **param** `name the name to greet`
```cpp
explicit Greeter(const char* name);
```

### `Greeter::greet`

Returns a greeting for the configured name.
- **returns** the greeting text
```cpp
const char* greet() const;
```

