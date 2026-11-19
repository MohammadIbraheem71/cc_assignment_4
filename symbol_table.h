#pragma once
#include <map>
#include <ostream>
#include <string>
#include <vector>

// the kinds of names that can be declared
enum SymbolKind {
    SYMBOL_VARIABLE,
    SYMBOL_PARAMETER,
    SYMBOL_FUNCTION
};

// the value types used by the c-- language, plus an error type
enum ValueType {
    TYPE_INT,
    TYPE_CHAR,
    TYPE_ERROR
};

// one row of the symbol table
struct SymbolEntry {
    std::string name;
    SymbolKind kind = SYMBOL_VARIABLE;
    ValueType type = TYPE_ERROR;
    std::vector<ValueType> parameter_types;   // only used for functions
    std::string unique_name;
    std::string owner_function;               // empty for globals and functions
    int scope_depth = 0;
};

class SymbolTable {
public:
    // the constructor starts the global scope
    SymbolTable();

    void enter_scope();

    // the global scope is never removed
    void exit_scope();

    // declares a name in the current scope, false means it is already there.
    // a succesful declaration also fills in entry.unique_name
    bool declare(SymbolEntry& entry);

    // searches from the innermost scope outward, nullptr if not found
    const SymbolEntry* lookup(const std::string& name) const;

    // every successful declaration, kept even after its scope is popped
    const std::vector<SymbolEntry>& get_declared_entries() const {
        return declared_entries;
    }

    // writes the retained entries as a plain text table
    void write(std::ostream& out) const;

private:
    // a stack of maps
    // each map represents a scope
    // a scope is popped when it is exited
    std::vector<std::map<std::string, SymbolEntry>> scope_stack;
    std::map<std::string, int> name_counter;
    std::vector<SymbolEntry> declared_entries;
};
