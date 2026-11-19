#include "symbol_table.h"

SymbolTable::SymbolTable() {
    enter_scope();
}

void SymbolTable::enter_scope() {
    scope_stack.push_back(std::map<std::string, SymbolEntry>());
}

void SymbolTable::exit_scope() {
    // keep at least the global scope on the stack
    if (scope_stack.size() > 1) {
        scope_stack.pop_back();
    }
}

// adds an entry to the sumbol table
bool SymbolTable::declare(SymbolEntry& entry) {
    std::map<std::string, SymbolEntry>& current_scope = scope_stack.back();

    // a name can only be declared once in the same scope
    if (current_scope.count(entry.name) > 0) {
        return false;
    }

    // the first name keeps its name, later ones get a number suffix
    int count = name_counter[entry.name] + 1;
    name_counter[entry.name] = count;
    if (count == 1) {
        entry.unique_name = entry.name;
    } else {
        entry.unique_name = entry.name + "_" + std::to_string(count);
    }

    entry.scope_depth = static_cast<int>(scope_stack.size());
    current_scope[entry.name] = entry;
    declared_entries.push_back(entry);
    return true;
}

const SymbolEntry* SymbolTable::lookup(const std::string& name) const {
    // start from the innermost scope and work outward
    for (int i = static_cast<int>(scope_stack.size()) - 1; i >= 0; --i) {
        std::map<std::string, SymbolEntry>::const_iterator found =
            scope_stack[i].find(name);
        if (found != scope_stack[i].end()) {
            return &found->second;
        }
    }
    return nullptr;
}

static std::string kind_name(SymbolKind kind) {
    if (kind == SYMBOL_PARAMETER) return "parameter";
    if (kind == SYMBOL_FUNCTION) return "function";
    return "variable";
}

static std::string type_name(ValueType type) {
    if (type == TYPE_INT) return "int";
    if (type == TYPE_CHAR) return "char";
    return "error";
}

void SymbolTable::write(std::ostream& out) const {
    out << "scope owner name unique_name kind type params\n";
    for (const SymbolEntry& entry : declared_entries) {
        out << entry.scope_depth << " "
            << (entry.owner_function.empty() ? "-" : entry.owner_function) << " "
            << entry.name << " " << entry.unique_name << " "
            << kind_name(entry.kind) << " " << type_name(entry.type);
        for (ValueType parameter : entry.parameter_types) {
            out << " " << type_name(parameter);
        }
        out << "\n";
    }
}
