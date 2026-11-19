#include "interpreter.h"

#include <cctype>
#include <iostream>
#include <stdexcept>

// unused quadruple fields are printed as "_"
static bool is_unused(const std::string& text) {
    return text == "_";
}

Interpreter::Interpreter(const std::vector<Quadruple>& code,
                         const SymbolTable& symbols)
    : code(code) {
    // labels and functions are resolved once, before execution starts
    for (std::size_t i = 0; i < code.size(); ++i) {
        const Quadruple& quadruple = code[i];
        if (quadruple.op == "label") {
            label_pc[quadruple.result] = i;
        } else if (quadruple.op == "func") {
            function_pc[quadruple.arg1] = i;
        }
    }

    // keep global storage separate from the per-function layouts
    for (const SymbolEntry& entry : symbols.get_declared_entries()) {
        if (entry.kind == SYMBOL_FUNCTION) continue;

        VariableInfo info;
        info.unique_name = entry.unique_name;
        info.type = entry.type;
        info.is_parameter = (entry.kind == SYMBOL_PARAMETER);

        if (entry.owner_function.empty()) {
            globals[entry.unique_name] = 0;
            global_types[entry.unique_name] = entry.type;
        } else {
            local_layouts[entry.owner_function].push_back(info);
        }
    }
}

void Interpreter::make_frame(const std::string& function, std::size_t return_pc,
                             const std::string& return_dest,
                             ActivationRecord& frame) const {
    frame.function = function;
    frame.return_pc = return_pc;
    frame.return_dest = return_dest;
    frame.values.clear();
    frame.pending_args.clear();

    std::map<std::string, std::vector<VariableInfo>>::const_iterator layout =
        local_layouts.find(function);
    if (layout == local_layouts.end()) return;

    for (const VariableInfo& variable : layout->second) {
        frame.values[variable.unique_name] = 0;
    }
}

int Interpreter::evaluate(const std::string& place,
                          const ActivationRecord& frame) const {
    if (is_unused(place)) return 0;

    // integer literals come straight from the source, negative values use unary minus
    if (!place.empty() && std::isdigit(static_cast<unsigned char>(place[0]))) {
        return std::stoi(place);
    }

    // character literals are stored with quotes, for example 'a'
    if (place.size() == 3 && place[0] == '\'' && place[2] == '\'') {
        return static_cast<unsigned char>(place[1]);
    }

    std::map<std::string, int>::const_iterator local = frame.values.find(place);
    if (local != frame.values.end()) return local->second;

    std::map<std::string, int>::const_iterator global = globals.find(place);
    if (global != globals.end()) return global->second;

    throw std::runtime_error("unknown value '" + place + "'");
}

void Interpreter::store(const std::string& place, int value,
                        ActivationRecord& frame) {
    std::map<std::string, int>::iterator global = globals.find(place);
    if (global == globals.end()) {
        frame.values[place] = value;
        return;
    }

    std::map<std::string, ValueType>::const_iterator type = global_types.find(place);
    if (type != global_types.end() && type->second == TYPE_CHAR) {
        value = static_cast<unsigned char>(value);
    }
    global->second = value;
}

int Interpreter::run() {
    std::map<std::string, std::size_t>::const_iterator start = function_pc.find("main");
    if (start == function_pc.end()) {
        std::cerr << "runtime error: no main function\n";
        return 1;
    }

    ActivationRecord main_frame;
    make_frame("main", code.size(), "_", main_frame);
    frames.push_back(main_frame);

    std::size_t pc = start->second + 1;   // skip the func marker
    int result = 0;

    try {
        while (!frames.empty() && pc < code.size()) {
            const Quadruple& quadruple = code[pc];
            ActivationRecord& frame = frames.back();

            if (quadruple.op == "func" || quadruple.op == "label") {
                ++pc;
                continue;
            }

            // falling off the end of a function acts like returning zero
            if (quadruple.op == "endfunc") {
                std::string return_dest = frame.return_dest;
                std::size_t return_pc = frame.return_pc;
                frames.pop_back();
                if (frames.empty()) {
                    result = 0;
                    break;
                }
                if (return_dest != "_") {
                    frames.back().values[return_dest] = 0;
                }
                pc = return_pc;
                continue;
            }

            if (quadruple.op == "=") {
                store(quadruple.result, evaluate(quadruple.arg1, frame), frame);
                ++pc;
                continue;
            }

            if (quadruple.op == "if_false") {
                if (evaluate(quadruple.arg1, frame) == 0) {
                    std::map<std::string, std::size_t>::const_iterator target =
                        label_pc.find(quadruple.result);
                    if (target == label_pc.end()) {
                        throw std::runtime_error("unknown label '" + quadruple.result + "'");
                    }
                    pc = target->second + 1;   // skip the label quad
                } else {
                    ++pc;
                }
                continue;
            }

            if (quadruple.op == "goto") {
                std::map<std::string, std::size_t>::const_iterator target =
                    label_pc.find(quadruple.result);
                if (target == label_pc.end()) {
                    throw std::runtime_error("unknown label '" + quadruple.result + "'");
                }
                pc = target->second + 1;
                continue;
            }

            if (quadruple.op == "param") {
                frame.pending_args.push_back(evaluate(quadruple.arg1, frame));
                ++pc;
                continue;
            }

            if (quadruple.op == "call") {
                std::map<std::string, std::size_t>::const_iterator target =
                    function_pc.find(quadruple.arg1);
                if (target == function_pc.end()) {
                    throw std::runtime_error("unknown function '" + quadruple.arg1 + "'");
                }

                int argument_count = std::stoi(quadruple.arg2);
                std::vector<int> arguments = frame.pending_args;
                if (static_cast<int>(arguments.size()) != argument_count) {
                    throw std::runtime_error("function '" + quadruple.arg1 +
                                             "' got " + std::to_string(arguments.size()) +
                                             " arguments, expected " +
                                             std::to_string(argument_count));
                }
                frame.pending_args.clear();

                ActivationRecord callee;
                make_frame(quadruple.arg1, pc + 1, quadruple.result, callee);

                std::map<std::string, std::vector<VariableInfo>>::const_iterator layout =
                    local_layouts.find(quadruple.arg1);
                if (layout != local_layouts.end()) {
                    std::size_t argument = 0;
                    for (const VariableInfo& variable : layout->second) {
                        if (variable.is_parameter && argument < arguments.size()) {
                            callee.values[variable.unique_name] = arguments[argument++];
                        }
                    }
                }

                frames.push_back(callee);
                pc = target->second + 1;   // skip the func marker
                continue;
            }

            if (quadruple.op == "return") {
                int value = evaluate(quadruple.arg1, frame);
                std::string return_dest = frame.return_dest;
                std::size_t return_pc = frame.return_pc;
                frames.pop_back();
                if (frames.empty()) {
                    result = value;
                    break;
                }
                if (return_dest != "_") {
                    frames.back().values[return_dest] = value;
                }
                pc = return_pc;
                continue;
            }

            // unary operators have no second operand
            if (is_unused(quadruple.arg2)) {
                int operand = evaluate(quadruple.arg1, frame);
                int value = 0;
                if (quadruple.op == "-") {
                    value = -operand;
                } else if (quadruple.op == "!") {
                    value = (operand == 0) ? 1 : 0;
                } else {
                    throw std::runtime_error("unknown unary operator '" + quadruple.op + "'");
                }
                store(quadruple.result, value, frame);
                ++pc;
                continue;
            }

            int left = evaluate(quadruple.arg1, frame);
            int right = evaluate(quadruple.arg2, frame);
            int value = 0;

            if (quadruple.op == "+") {
                value = left + right;
            } else if (quadruple.op == "-") {
                value = left - right;
            } else if (quadruple.op == "*") {
                value = left * right;
            } else if (quadruple.op == "/") {
                if (right == 0) throw std::runtime_error("division by zero");
                value = left / right;
            } else if (quadruple.op == "%") {
                if (right == 0) throw std::runtime_error("division by zero");
                value = left % right;
            } else if (quadruple.op == "==") {
                value = (left == right) ? 1 : 0;
            } else if (quadruple.op == "!=") {
                value = (left != right) ? 1 : 0;
            } else if (quadruple.op == "<") {
                value = (left < right) ? 1 : 0;
            } else if (quadruple.op == ">") {
                value = (left > right) ? 1 : 0;
            } else if (quadruple.op == "<=") {
                value = (left <= right) ? 1 : 0;
            } else if (quadruple.op == ">=") {
                value = (left >= right) ? 1 : 0;
            } else if (quadruple.op == "&&") {
                value = (left != 0 && right != 0) ? 1 : 0;
            } else if (quadruple.op == "||") {
                value = (left != 0 || right != 0) ? 1 : 0;
            } else {
                throw std::runtime_error("unknown operator '" + quadruple.op + "'");
            }

            store(quadruple.result, value, frame);
            ++pc;
        }
    } catch (const std::exception& error) {
        std::cerr << "runtime error: " << error.what() << "\n";
        return 1;
    }

    std::cout << "program output: " << result << "\n";
    return 0;
}
