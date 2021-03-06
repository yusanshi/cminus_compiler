#include "cminus_builder.hpp"
#include <iostream>

using namespace llvm;
using namespace std;

#define CONSTi32(num) ConstantInt::get(context, APInt(32, num))

static Function *curr_function = nullptr;
static vector<string> curr_func_arg_names;
static bool is_outermost_compound = false;
static bool last_returned = false;
static bool is_passing_array = false;

static Value *curr_expression_value = nullptr;
static Value *curr_addi_value = nullptr;
static Value *curr_factor_value = nullptr;
static Value *curr_term_value = nullptr;

// Exit code
// 101: No function.
// 102: Not found.
// 103: Return type unmatched.
// 104: Length of declared array is negative.

void CminusBuilder::visit(syntax_program &node) {
    for (auto d : node.declarations) {
        d->accept(*this);
    }
}

void CminusBuilder::visit(syntax_num &node) {
    curr_factor_value = CONSTi32(node.value);
}

void CminusBuilder::visit(syntax_var_declaration &node) {
    GlobalVariable *gv;
    auto int_type = Type::getInt32Ty(this->context);
    if (!node.num.get()) {
        auto int_init = ConstantAggregateZero::get(int_type);
        gv = new GlobalVariable(*this->module.get(), int_type, false,
                                GlobalValue::LinkageTypes::CommonLinkage,
                                int_init, node.id);
    } else if (node.num->value < 0) {
        cerr << "syntax_var_declaration: array length is negative\n";
        exit(104);
    } else {
        auto array_type = ArrayType::get(int_type, node.num->value);
        auto array_init = ConstantAggregateZero::get(array_type);
        gv = new GlobalVariable(*this->module.get(), array_type, false,
                                GlobalValue::LinkageTypes::CommonLinkage,
                                array_init, node.id);
    }
    this->scope.push(node.id, gv);
}

void CminusBuilder::visit(syntax_fun_declaration &node) {
    auto type = (node.type == TYPE_INT) ? Type::getInt32Ty(this->context)
                                        : Type::getVoidTy(this->context);
    curr_func_arg_names.clear();
    Function *func;
    if (node.params.empty()) {
        func = Function::Create(FunctionType::get(type, false),
                                GlobalValue::LinkageTypes::ExternalLinkage,
                                node.id, this->module.get());
    } else {
        vector<Type *> params;
        for (auto p : node.params) {
            curr_func_arg_names.push_back(p->id);
            if (p->isarray) {
                params.push_back(Type::getInt32PtrTy(this->context));
            } else {
                params.push_back(Type::getInt32Ty(this->context));
            }
        }
        func = Function::Create(FunctionType::get(type, params, false),
                                GlobalValue::LinkageTypes::ExternalLinkage,
                                node.id, this->module.get());
    }
    this->scope.push(node.id, func);

    is_outermost_compound = true;
    curr_function = func;
    node.compound_stmt->accept(*this);
}

void CminusBuilder::visit(syntax_param &node) {
    // Not needed
}

void CminusBuilder::visit(syntax_compound_stmt &node) {
    if (!curr_function) {
        cerr << "No function\n";
        exit(101);
    }

    auto i32_t = Type::getInt32Ty(this->context);

    this->scope.enter();
    if (is_outermost_compound) {
        auto entry = BasicBlock::Create(this->context, "", curr_function);
        this->builder.SetInsertPoint(entry);
        is_outermost_compound = false;

        int i = 0;
        for (auto it = curr_function->arg_begin();
             it != curr_function->arg_end(); it++, i++) {
            auto alloc = this->builder.CreateAlloca(it->getType());
            this->builder.CreateStore(it, alloc);
            this->scope.push(curr_func_arg_names[i], alloc);
        }
        curr_func_arg_names.clear();
    }

    for (auto d : node.local_declarations) {
        auto decl = d.get();
        if (decl->num.get()) {
            // Array
            auto type = ArrayType::get(i32_t, decl->num->value);
            auto val = this->builder.CreateAlloca(type);
            this->scope.push(decl->id, val);
        } else {
            // Int
            auto val = this->builder.CreateAlloca(i32_t);
            this->scope.push(decl->id, val);
        }
    }

    for (auto s : node.statement_list) {
        s->accept(*this);
    }
    // Pass on the last_returned

    this->scope.exit();

    if (this->scope.in_global()) {
        auto void_t = Type::getVoidTy(this->context);
        if (curr_function->getReturnType() == void_t) {
            this->builder.CreateRetVoid();
        }
    }
}

void CminusBuilder::visit(syntax_expresion_stmt &node) {
    if (node.expression.get())
        node.expression->accept(*this);
    last_returned = false;
}

void CminusBuilder::visit(syntax_selection_stmt &node) {
    node.expression->accept(*this);
    auto icmp = this->builder.CreateICmpNE(curr_expression_value, CONSTi32(0));
    curr_expression_value = nullptr;

    auto if_true = BasicBlock::Create(this->context, "if.true");
    auto if_false = BasicBlock::Create(this->context, "if.false");
    auto if_end = BasicBlock::Create(this->context, "if.end");

    if (!node.else_statement.get()) {
        // br cond, true, end
        // true:
        // ...
        // br end
        // end:
        if_true->insertInto(curr_function);
        if_end->insertInto(curr_function);
        this->builder.CreateCondBr(icmp, if_true, if_end);
        this->builder.SetInsertPoint(if_true);
        node.if_statement->accept(*this);
        if (!last_returned)
            this->builder.CreateBr(if_end);
        this->builder.SetInsertPoint(if_end);
        last_returned = false;
    } else {
        // br cond, true, false
        // true:
        // ...
        // br end
        // false:
        // ...
        // br end
        // end:
        if_true->insertInto(curr_function);
        if_false->insertInto(curr_function);
        this->builder.CreateCondBr(icmp, if_true, if_false);
        this->builder.SetInsertPoint(if_true);
        node.if_statement->accept(*this);
        auto then_returned = last_returned;
        if (!then_returned)
            this->builder.CreateBr(if_end);
        this->builder.SetInsertPoint(if_false);
        node.else_statement->accept(*this);
        auto else_returned = last_returned;
        if (!else_returned)
            this->builder.CreateBr(if_end);
        this->builder.SetInsertPoint(if_end);
        last_returned = then_returned && else_returned;
        if (!last_returned) {
            if_end->insertInto(curr_function);
        }
    }
}

void CminusBuilder::visit(syntax_iteration_stmt &node) {
    auto while_judge =
        BasicBlock::Create(this->context, "while.judge", curr_function);
    auto while_continue =
        BasicBlock::Create(this->context, "while.continue", curr_function);
    auto while_end =
        BasicBlock::Create(this->context, "while.end", curr_function);

    // br while.judge
    // while.judge:
    // ...
    // br cond while.continue while.end
    // while.continue:
    // ...
    // br while.judge
    // while.end:
    this->builder.CreateBr(while_judge);
    this->builder.SetInsertPoint(while_judge);
    node.expression->accept(*this);
    auto icmp = this->builder.CreateICmpNE(curr_expression_value, CONSTi32(0));
    curr_expression_value = nullptr;
    this->builder.CreateCondBr(icmp, while_continue, while_end);
    this->builder.SetInsertPoint(while_continue);
    node.statement->accept(*this);
    if (!last_returned)
        this->builder.CreateBr(while_judge);
    this->builder.SetInsertPoint(while_end);
    last_returned = false;
}

void CminusBuilder::visit(syntax_return_stmt &node) {
    if (node.expression) {  // return non-void;
        if (curr_function->getReturnType() == Type::getVoidTy(this->context)) {
            // Function defined to return void but actually return a non-void
            // value.
            cerr << "Void function should not return a value\n";
            exit(103);
        } else {
            node.expression->accept(*this);
            this->builder.CreateRet(curr_expression_value);
            curr_expression_value = nullptr;
        }
    } else {  // return;
        if (curr_function->getReturnType() == Type::getVoidTy(this->context)) {
            this->builder.CreateRetVoid();
        } else {
            // Function defined to return int but actually "return;"
            cerr << "Non-void function should return a value\n";
            exit(103);
        }
    }
    last_returned = true;
}

void CminusBuilder::visit(syntax_var &node) {
    auto var_ptr = this->scope.find(node.id);
    if (!var_ptr) {
        cerr << "Name " << node.id << " not found\n";
        exit(102);
    }
    if (is_passing_array) {
        curr_factor_value = var_ptr;
    } else {
        if (node.expression.get()) {
            node.expression->accept(*this);
            auto expression_value = curr_expression_value;
            curr_expression_value = nullptr;

            // Runtime negative index check
            // (But why need we do this while stanard C implementation does not?
            // I think add selection here is UGLY.)
            auto icmp =
                this->builder.CreateICmpSLT(expression_value, CONSTi32(0));
            auto neg_idx_true = BasicBlock::Create(
                this->context, "neg.idx.true", curr_function);
            auto neg_idx_end =
                BasicBlock::Create(this->context, "neg.idx.end", curr_function);
            this->builder.CreateCondBr(icmp, neg_idx_true, neg_idx_end);
            this->builder.SetInsertPoint(neg_idx_true);
            auto neg_idx_except = this->scope.find("neg_idx_except");
            this->builder.CreateCall(neg_idx_except);
            this->builder.CreateBr(neg_idx_end);
            this->builder.SetInsertPoint(neg_idx_end);

            auto i32p_t = Type::getInt32PtrTy(this->context);
            auto i32pp_t = i32p_t->getPointerTo();
            if (var_ptr->getType() == i32pp_t) {
                var_ptr = this->builder.CreateLoad(i32p_t, var_ptr);
                var_ptr =
                    this->builder.CreateInBoundsGEP(var_ptr, expression_value);
            } else {
                var_ptr = this->builder.CreateInBoundsGEP(
                    var_ptr, vector<Value *>{CONSTi32(0), expression_value});
            }
        }
        curr_factor_value = this->builder.CreateLoad(var_ptr);
    }
}

void CminusBuilder::visit(syntax_assign_expression &node) {
    node.expression->accept(*this);
    auto expression_value_saved = curr_expression_value;
    curr_expression_value = nullptr;
    if (node.var.get()) {
        auto var_ptr = this->scope.find(node.var->id);
        if (!var_ptr) {
            cerr << "Name " << node.var->id << " not found\n";
            exit(102);
        }
        if (node.var->expression.get()) {
            node.var->expression->accept(*this);
            auto expression_value = curr_expression_value;
            curr_expression_value = nullptr;

            auto icmp =
                this->builder.CreateICmpSLT(expression_value, CONSTi32(0));
            auto neg_idx_true = BasicBlock::Create(
                this->context, "neg.idx.true", curr_function);
            auto neg_idx_end =
                BasicBlock::Create(this->context, "neg.idx.end", curr_function);
            this->builder.CreateCondBr(icmp, neg_idx_true, neg_idx_end);
            this->builder.SetInsertPoint(neg_idx_true);
            auto neg_idx_except = this->scope.find("neg_idx_except");
            this->builder.CreateCall(neg_idx_except);
            this->builder.CreateBr(neg_idx_end);
            this->builder.SetInsertPoint(neg_idx_end);

            auto i32p_t = Type::getInt32PtrTy(this->context);
            auto i32pp_t = i32p_t->getPointerTo();
            if (var_ptr->getType() == i32pp_t) {
                var_ptr = this->builder.CreateLoad(i32p_t, var_ptr);
                var_ptr =
                    this->builder.CreateInBoundsGEP(var_ptr, expression_value);
            } else {
                var_ptr = this->builder.CreateInBoundsGEP(
                    var_ptr, vector<Value *>{CONSTi32(0), expression_value});
            }
        }
        this->builder.CreateStore(expression_value_saved, var_ptr);
    }
    curr_factor_value = expression_value_saved;
    curr_expression_value = expression_value_saved;
}

void CminusBuilder::visit(syntax_simple_expression &node) {
    auto left_addi = node.additive_expression_l.get();

    left_addi->accept(*this);
    auto left_value = curr_addi_value;
    curr_addi_value = nullptr;

    auto right_addi = node.additive_expression_r.get();
    if (!right_addi) {
        curr_expression_value = left_value;
        curr_factor_value = left_value;
        return;
    }

    right_addi->accept(*this);
    auto right_value = curr_addi_value;
    curr_addi_value = nullptr;

    Value *icmp;
    switch (node.op) {
    case OP_LE:  // <=
        icmp = this->builder.CreateICmpSLE(left_value, right_value);
        break;
    case OP_LT:  // <
        icmp = this->builder.CreateICmpSLT(left_value, right_value);
        break;
    case OP_GT:  // >
        icmp = this->builder.CreateICmpSGT(left_value, right_value);
        break;
    case OP_GE:  // >=
        icmp = this->builder.CreateICmpSGE(left_value, right_value);
        break;
    case OP_EQ:  // ==
        icmp = this->builder.CreateICmpEQ(left_value, right_value);
        break;
    case OP_NEQ:  // !=
        icmp = this->builder.CreateICmpNE(left_value, right_value);
        break;
    }
    auto i32_t = Type::getInt32Ty(this->context);
    auto simp_expr_val = this->builder.CreateZExt(icmp, i32_t);

    curr_expression_value = simp_expr_val;
    curr_factor_value = simp_expr_val;
}

void CminusBuilder::visit(syntax_additive_expression &node) {
    node.term->accept(*this);
    auto term_value = curr_term_value;
    curr_term_value = nullptr;

    auto expr = node.additive_expression.get();
    if (!expr) {
        curr_addi_value = term_value;
        return;
    }

    expr->accept(*this);
    auto addi_value = curr_addi_value;
    curr_addi_value = nullptr;

    if (node.op == OP_PLUS) {
        curr_addi_value = this->builder.CreateNSWAdd(addi_value, term_value);
    } else if (node.op == OP_MINUS) {
        curr_addi_value = this->builder.CreateNSWSub(addi_value, term_value);
    }
}

void CminusBuilder::visit(syntax_term &node) {
    node.factor->accept(*this);
    auto factor_value = curr_factor_value;
    curr_factor_value = nullptr;

    if (node.term.get()) {
        node.term->accept(*this);
        if (node.op == OP_MUL) {
            curr_term_value =
                this->builder.CreateNSWMul(curr_term_value, factor_value);
        } else if (node.op == OP_DIV) {
            curr_term_value =
                this->builder.CreateSDiv(curr_term_value, factor_value);
        }
    } else {
        curr_term_value = factor_value;
    }
}

void CminusBuilder::visit(syntax_call &node) {
    Function *call_func = cast<Function>(this->scope.find(node.id));
    if (!call_func) {
        cerr << "Name " << node.id << " not found\n";
        exit(102);
    }

    if (node.args.empty()) {
        auto ret = this->builder.CreateCall(call_func);
        curr_factor_value = ret;
        return;
    }

    auto i32p_t = Type::getInt32PtrTy(this->context);
    auto i32pp_t = i32p_t->getPointerTo();
    vector<Value *> call_args;
    for (auto i = 0ul; i < node.args.size(); i++) {
        auto arg = call_func->arg_begin() + i;
        if (arg->getType() == Type::getInt32PtrTy(this->context)) {
            // In order to judge whether current arg is array,
            // My strategy here is to query call_func's arg
            // So compiler don't know whether user is passing
            // a non-ptr value to a ptr parameter
            is_passing_array = true;
        }
        node.args[i]->accept(*this);
        if (is_passing_array) {
            if (curr_expression_value->getType() == i32pp_t) {
                auto val = this->builder.CreateLoad(curr_expression_value);
                call_args.push_back(val);
            } else {
                auto val = this->builder.CreateInBoundsGEP(
                    curr_expression_value,
                    vector<Value *>{CONSTi32(0), CONSTi32(0)});
                call_args.push_back(val);
            }
            is_passing_array = false;
        } else {
            call_args.push_back(curr_expression_value);
        }
        curr_expression_value = nullptr;
    }

    auto ret = this->builder.CreateCall(call_func, call_args);
    curr_factor_value = ret;
}
