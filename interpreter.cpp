#include "interpreter.h"

#include <cctype>
#include <iostream>
#include <stdexcept>

// this file runs the quadruples that the analyser gives us.
// each quadruple is like (op, arg1, arg2, result). most ops put an answer into
// result, while jumps put a label name there instead.
//
// there are two places where values live. globals are shared by the whole
// program, and frames are one little record for each active function call.
//
// a frame keeps the parameters, local names, temps, the spot to come back to,
// and the place where a retrun value goes. becuase each call gets its own frame,
// recursion does not mix up the values of different calls.

// empty fields are written as "_" by the analyser. this helps us tell the
// diffrence between "nothing here" and a real name like x or t1.
static bool is_unused(const std::string& text) {
    return text == "_";
}

// the constructor only sets things up, it does not run the program yet.
Interpreter::Interpreter(const std::vector<Quadruple>& code,
                         const SymbolTable& symbols)
    : code(code) {
    // we walk the quadruples once and remember where each label and each func
    // marker sits. later a jump can find its target by name and a call can go
    // straight to the right place without searching the whole list again.
    for (std::size_t i = 0; i < code.size(); ++i) {
        const Quadruple& quadruple = code[i];
        if (quadruple.op == "label") {
            label_pc[quadruple.result] = i;
        } else if (quadruple.op == "func") {
            function_pc[quadruple.arg1] = i;
        }
    }

    // the symbol table tells us wich names are global and wich ones belong to
    // a function. we copy that info because analysis scopes get popped after
    // use, but we still need the layout when a call happens later on.
    for (const SymbolEntry& entry : symbols.get_declared_entries()) {
        if (entry.kind == SYMBOL_FUNCTION) continue;

        VariableInfo info;
        info.unique_name = entry.unique_name;
        info.type = entry.type;
        info.is_parameter = (entry.kind == SYMBOL_PARAMETER);

        // function rows are skipped here, we only need them as call targets.
        // a name with no owner is global and gets one shared slot. a owned name
        // goes into the frame of that function, and parameters are marked so a
        // call can fill them in the right order.
        if (entry.owner_function.empty()) {
            globals[entry.unique_name] = 0;
            global_types[entry.unique_name] = entry.type;
        } else {
            local_layouts[entry.owner_function].push_back(info);
        }
    }
}

// build a fresh record for one call. every local name and temp starts at 0.
// we also save where to continue after the function is done and where its
// retrun value should be written in the caller. a retrun spot of "_" means
// the caller does not care about the value.
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

// work out the integer value of one operand. a place can be "_", a number,
// a char literal with quotes, or a variable or temp name. all values are kept
// as plain integers, so a char is just its ascii code.
int Interpreter::evaluate(const std::string& place,
                          const ActivationRecord& frame) const {
    if (is_unused(place)) return 0;

    // negitive numbers are not their own literal in c--, the analyser emits a
    // unary minus, so a number always starts with a digit.
    if (!place.empty() && std::isdigit(static_cast<unsigned char>(place[0]))) {
        return std::stoi(place);
    }

    // char literals keep the quotes in the text, like 'a'. the middle char is
    // the value we want.
    if (place.size() == 3 && place[0] == '\'' && place[2] == '\'') {
        return static_cast<unsigned char>(place[1]);
    }

    // the current frame is checked first, so a local copy wins over a global
    // with the same name. then we try the shared globals.
    std::map<std::string, int>::const_iterator local = frame.values.find(place);
    if (local != frame.values.end()) return local->second;

    std::map<std::string, int>::const_iterator global = globals.find(place);
    if (global != globals.end()) return global->second;

    // if the name is not in either place then the quadruples dont match the
    // symbol table, so we stop.
    throw std::runtime_error("unknown value '" + place + "'");
}

// writing works the same way as reading. a global name is updated for
// everyone, otherwise the value goes into the current frame. this is how a
// temp like t1 can exist separatly in two calls without clashing.
void Interpreter::store(const std::string& place, int value,
                        ActivationRecord& frame) {
    std::map<std::string, int>::iterator global = globals.find(place);
    if (global == globals.end()) {
        frame.values[place] = value;
        return;
    }

    // char globals are kept to byte sized values. the analyser mostly blocks
    // bad int to char assignments, this just keeps things tidy.
    std::map<std::string, ValueType>::const_iterator type = global_types.find(place);
    if (type != global_types.end() && type->second == TYPE_CHAR) {
        value = static_cast<unsigned char>(value);
    }
    global->second = value;
}

// this is the main loop. we find main, make its first frame, then walk the
// quadruple list with pc. frame always means the call we are in right now.
// most ops just do there job and move on, jumps and retruns change pc.
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

    // bad values, missing labels or divide by zero all throw. we catch them
    // here, print a short note and stop with a failing exit code.
    try {
        while (!frames.empty() && pc < code.size()) {
            const Quadruple& quadruple = code[pc];
            ActivationRecord& frame = frames.back();

            // these are just markers used when we built the lookup tables.
            // there is nothing to do here, so step over them.
            if (quadruple.op == "func" || quadruple.op == "label") {
                ++pc;
                continue;
            }

            // if a function falls off the end with no retrun we treat it as
            // giving back 0. pop its frame. if there is a caller we give it 0
            // and go back to the saved spot, otherwise the whole program is done.
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

            // copy instruction, work out the source and put it in the result slot.
            if (quadruple.op == "=") {
                store(quadruple.result, evaluate(quadruple.arg1, frame), frame);
                ++pc;
                continue;
            }

            // conditional jump. if the condition is 0 we go to the label in
            // result. if it is anything else we simply keep going
            // we use if_false instead of if_true because when the condition is true
            // we can just fall through without going to any labels
            // but IF IT IS FALSE, we need to go to the label
            // kinda like if false then go to else label
            // hope u understand , thanks
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

            // unconditional jump. the plus one skips the label marker so we
            // start at the real instruction after it.
            if (quadruple.op == "goto") {
                std::map<std::string, std::size_t>::const_iterator target =
                    label_pc.find(quadruple.result);
                if (target == label_pc.end()) {
                    throw std::runtime_error("unknown label '" + quadruple.result + "'");
                }
                pc = target->second + 1;
                continue;
            }

            // arguments are worked out before the call and queued up in source
            // order. the call instruction that follows will hand them to the
            // new frame.
            if (quadruple.op == "param") {
                frame.pending_args.push_back(evaluate(quadruple.arg1, frame));
                ++pc;
                continue;
            }

            // a call does a few things at once. it finds the first instruction
            // of the function, takes the arguments that param put in the queue,
            // makes a fresh frame, and matches those arguments to the parameters
            // in declaration order. it also saves where to come back to.
            //
            // if the call is part of an expression then result is a temp in the
            // caller and the retrun value lands there later. for a plain call
            // statement result is "_" and the value is thrown away.
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

                // the layout list is in source order, so the first parameter
                // gets the first argument and so on.
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

                // push the new frame and jump in. the old frame stays underneath
                // with its comeback spot already saved.
                frames.push_back(callee);
                pc = target->second + 1;   // skip the func marker
                continue;
            }

            // work out the value, then pop the current frame. if nobody is left
            // underneath then this was main and the value is the programs
            // output. otherwise we write it into the caller saved slot and
            // resume at the saved spot.
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

            // unary ops have no second operand so arg2 is "_". the analyser
            // only makes unary minus and not.
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

            // binary ops have two operands. arithmetic is normal, comparisons
            // and logical ops give 1 for true and 0 for false. divide or modulo
            // by zero is a runtime error.
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

            // put the answer into result temp and carry on.
            store(quadruple.result, value, frame);
            ++pc;
        }
    } catch (const std::exception& error) {
        std::cerr << "runtime error: " << error.what() << "\n";
        return 1;
    }

    // the loop stops when main is done. print its value in the wanted format.
    std::cout << "program output: " << result << "\n";
    return 0;
}
