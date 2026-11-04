#pragma once

namespace Sample {

/// \class Greeter
/// \brief Produces greeting messages.
class Greeter {
public:
    /// Constructs a greeter with the given name.
    /// \param name the name to greet
    explicit Greeter(const char* name);

    /// Returns a greeting for the configured name.
    /// \return the greeting text
    const char* greet() const;
};

} // namespace Sample
