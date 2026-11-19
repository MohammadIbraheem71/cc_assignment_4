#pragma once
#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include "analyzer.h"
#include "symbol_table.h"

// Executes the quadruples produced by Analyzer using the completed symbol table.
class Interpreter {
public:
    Interpreter(const std::vector<Quadruple>& code, const SymbolTable& symbols);

    // Runs main and prints "program output: <value>". Returns 0 on success.
    int run();

private:
    struct VariableInfo {
        std::string unique_name;
        ValueType type;
        bool is_parameter;
    };

    struct ActivationRecord {
        std::string function;
        std::size_t return_pc;
        std::string return_dest;
        std::map<std::string, int> values;
        std::vector<int> pending_args;
    };

    int evaluate(const std::string& place, const ActivationRecord& frame) const;
    void store(const std::string& place, int value, ActivationRecord& frame);
    void make_frame(const std::string& function, std::size_t return_pc,
                    const std::string& return_dest, ActivationRecord& frame) const;

    std::vector<Quadruple> code;
    std::map<std::string, std::size_t> label_pc;
    std::map<std::string, std::size_t> function_pc;
    std::map<std::string, int> globals;
    std::map<std::string, ValueType> global_types;
    std::map<std::string, std::vector<VariableInfo>> local_layouts;
    std::vector<ActivationRecord> frames;
};
