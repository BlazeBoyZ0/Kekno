#include "compiler.h"
#include <iostream>

Compiler::Compiler(const std::string& src, Chunk& targetChunk)
    : lexer(src), targetChunk(targetChunk) {
    advance();
}

void Compiler::advance() {
    prev = current;
    current = lexer.nextToken();
    if (current.type == TokenType::ERROR) {
        std::cout << "[Syntax Error]: " << current.text << std::endl;
        hasError = true;
    }
}

bool Compiler::match(TokenType type) {
    if (current.type == type) {
        advance();
        return true;
    }
    return false;
}

void Compiler::consume(TokenType type, const std::string& errMsg) {
    if (current.type == type) {
        advance();
    } else {
        std::string found = (current.type == TokenType::END_OF_FILE) ? "EOF" : current.text;
        std::cout << "[Syntax Error]: " << errMsg << " (Found: '" << found << "')" << std::endl;
        hasError = true;
    }
}

void Compiler::emitConstant(Value value) {
    chunk().writeOp(OpCode::OP_CONSTANT);
    chunk().writeByte(chunk().addConstant(value));
}

int Compiler::emitJump(OpCode op) {
    chunk().writeOp(op);
    chunk().writeByte(0xff);
    chunk().writeByte(0xff);
    return static_cast<int>(chunk().code.size() - 2);
}

void Compiler::patchJump(int offset) {
    int jump = static_cast<int>(chunk().code.size() - offset - 2);
    if (jump > 0xffff) {
        std::cout << "[Compiler Error]: Too much code to jump over." << std::endl;
        hasError = true;
        return;
    }
    chunk().code[offset] = static_cast<uint8_t>((jump >> 8) & 0xff);
    chunk().code[offset + 1] = static_cast<uint8_t>(jump & 0xff);
}

void Compiler::emitLoop(int loopStart) {
    chunk().writeOp(OpCode::OP_LOOP);
    int jump = static_cast<int>(chunk().code.size() - loopStart + 2);
    if (jump > 0xffff) {
        std::cout << "[Compiler Error]: Loop body too large." << std::endl;
        hasError = true;
        return;
    }
    chunk().writeByte(static_cast<uint8_t>((jump >> 8) & 0xff));
    chunk().writeByte(static_cast<uint8_t>(jump & 0xff));
}

void Compiler::beginScope() {
    currentContext->scopeDepth++;
}

void Compiler::endScope() {
    currentContext->scopeDepth--;
    while (currentContext->localCount > 0 &&
           currentContext->locals[currentContext->localCount - 1].depth > currentContext->scopeDepth) {
        chunk().writeOp(OpCode::OP_POP);
        currentContext->localCount--;
    }
}

void Compiler::addLocal(const std::string& name) {
    if (currentContext->localCount >= 256) {
        std::cout << "[Compiler Error]: Too many local variables in function." << std::endl;
        hasError = true;
        return;
    }

    for (int i = currentContext->localCount - 1; i >= 0; i--) {
        Local* local = &currentContext->locals[i];
        if (local->depth != -1 && local->depth < currentContext->scopeDepth) {
            break;
        }
        if (local->name == name) {
            std::cout << "[Compiler Error]: Variable with name '" << name << "' already declared in this scope." << std::endl;
            hasError = true;
            return;
        }
    }

    Local* local = &currentContext->locals[currentContext->localCount++];
    local->name = name;
    local->depth = currentContext->scopeDepth;
}

int Compiler::resolveLocal(CompilerContext* context, const std::string& name) {
    for (int i = context->localCount - 1; i >= 0; i--) {
        if (context->locals[i].name == name) {
            return i;
        }
    }
    return -1;
}

uint8_t Compiler::argumentList() {
    uint8_t argCount = 0;
    if (current.type != TokenType::RPAREN) {
        do {
            expression();
            if (argCount == 255) {
                std::cout << "[Compiler Error]: Cannot have more than 255 arguments." << std::endl;
                hasError = true;
            }
            argCount++;
        } while (match(TokenType::COMMA));
    }
    consume(TokenType::RPAREN, "Expected ')' after arguments");
    return argCount;
}

void Compiler::primary() {
    if (hasError) return;
    if (current.type == TokenType::NUMBER) {
        emitConstant(Value(current.numValue));
        advance();
    } else if (current.type == TokenType::STRING) {
        emitConstant(Value(current.strValue));
        advance();
    } else if (current.type == TokenType::TRUE) {
        chunk().writeOp(OpCode::OP_TRUE);
        advance();
    } else if (current.type == TokenType::FALSE) {
        chunk().writeOp(OpCode::OP_FALSE);
        advance();
    } else if (current.type == TokenType::NIL) {
        chunk().writeOp(OpCode::OP_NIL);
        advance();
    } else if (current.type == TokenType::IDENTIFIER) {
        std::string name = current.text;
        advance();
        int localSlot = resolveLocal(currentContext, name);
        if (localSlot != -1) {
            chunk().writeOp(OpCode::OP_GET_LOCAL);
            chunk().writeByte(static_cast<uint8_t>(localSlot));
        } else {
            uint8_t nameIdx = chunk().addConstant(Value(name));
            chunk().writeOp(OpCode::OP_GET_GLOBAL);
            chunk().writeByte(nameIdx);
        }
    } else if (current.type == TokenType::LPAREN) {
        advance();
        expression();
        consume(TokenType::RPAREN, "Expected ')' after expression");
    } else if (current.type == TokenType::LBRACKET) {
        advance();
        uint8_t elementCount = 0;
        if (current.type != TokenType::RBRACKET) {
            do {
                expression();
                if (elementCount == 255) {
                    std::cout << "[Compiler Error]: Cannot have more than 255 elements in array literal." << std::endl;
                    hasError = true;
                }
                elementCount++;
            } while (match(TokenType::COMMA));
        }
        consume(TokenType::RBRACKET, "Expected ']' after array elements");
        chunk().writeOp(OpCode::OP_BUILD_ARRAY);
        chunk().writeByte(elementCount);
    } else if (current.type == TokenType::LBRACE) {
        advance();
        uint8_t entryCount = 0;
        if (current.type != TokenType::RBRACE) {
            do {
                expression();
                consume(TokenType::COLON, "Expected ':' after map key");
                expression();
                if (entryCount == 255) {
                    std::cout << "[Compiler Error]: Cannot have more than 255 entries in map literal." << std::endl;
                    hasError = true;
                }
                entryCount++;
            } while (match(TokenType::COMMA));
        }
        consume(TokenType::RBRACE, "Expected '}' after map entries");
        chunk().writeOp(OpCode::OP_BUILD_MAP);
        chunk().writeByte(entryCount);
    } else {
        std::cout << "[Syntax Error]: Expected expression" << std::endl;
        hasError = true;
    }
}

void Compiler::postfix() {
    primary();
    while (!hasError) {
        if (match(TokenType::LPAREN)) {
            uint8_t argCount = argumentList();
            chunk().writeOp(OpCode::OP_CALL);
            chunk().writeByte(argCount);
        } else if (match(TokenType::LBRACKET)) {
            expression();
            consume(TokenType::RBRACKET, "Expected ']' after index");
            chunk().writeOp(OpCode::OP_GET_INDEX);
        } else {
            break;
        }
    }
}

void Compiler::unary() {
    if (hasError) return;
    if (current.type == TokenType::BANG || current.type == TokenType::NOT) {
        advance();
        unary();
        chunk().writeOp(OpCode::OP_NOT);
    } else if (current.type == TokenType::MINUS) {
        advance();
        unary();
        emitConstant(Value(-1.0));
        chunk().writeOp(OpCode::OP_MULTIPLY);
    } else {
        postfix();
    }
}

void Compiler::power() {
    if (hasError) return;
    unary();
    if (current.type == TokenType::CARET) {
        advance();
        power();
        chunk().writeOp(OpCode::OP_POWER);
    }
}

void Compiler::factor() {
    if (hasError) return;
    power();
    while (current.type == TokenType::STAR || current.type == TokenType::SLASH || current.type == TokenType::PERCENT) {
        TokenType op = current.type;
        advance();
        power();
        if (op == TokenType::STAR) chunk().writeOp(OpCode::OP_MULTIPLY);
        else if (op == TokenType::SLASH) chunk().writeOp(OpCode::OP_DIVIDE);
        else chunk().writeOp(OpCode::OP_MODULO);
    }
}

void Compiler::term() {
    if (hasError) return;
    factor();
    while (current.type == TokenType::PLUS || current.type == TokenType::MINUS) {
        TokenType op = current.type;
        advance();
        factor();
        if (op == TokenType::PLUS) chunk().writeOp(OpCode::OP_ADD);
        else chunk().writeOp(OpCode::OP_SUBTRACT);
    }
}

void Compiler::relational() {
    if (hasError) return;
    term();
    while (current.type == TokenType::GREATER || current.type == TokenType::GREATER_EQUAL ||
           current.type == TokenType::LESS || current.type == TokenType::LESS_EQUAL) {
        TokenType op = current.type;
        advance();
        term();
        if (op == TokenType::GREATER) {
            chunk().writeOp(OpCode::OP_GREATER);
        } else if (op == TokenType::GREATER_EQUAL) {
            chunk().writeOp(OpCode::OP_LESS);
            chunk().writeOp(OpCode::OP_NOT);
        } else if (op == TokenType::LESS) {
            chunk().writeOp(OpCode::OP_LESS);
        } else if (op == TokenType::LESS_EQUAL) {
            chunk().writeOp(OpCode::OP_GREATER);
            chunk().writeOp(OpCode::OP_NOT);
        }
    }
}

void Compiler::equality() {
    if (hasError) return;
    relational();
    while (current.type == TokenType::EQUAL_EQUAL || current.type == TokenType::BANG_EQUAL) {
        TokenType op = current.type;
        advance();
        relational();
        if (op == TokenType::EQUAL_EQUAL) {
            chunk().writeOp(OpCode::OP_EQUAL);
        } else {
            chunk().writeOp(OpCode::OP_EQUAL);
            chunk().writeOp(OpCode::OP_NOT);
        }
    }
}

void Compiler::andExpression() {
    if (hasError) return;
    equality();
    while (current.type == TokenType::AND) {
        advance();
        int endJump = emitJump(OpCode::OP_JUMP_IF_FALSE);
        chunk().writeOp(OpCode::OP_POP);
        equality();
        patchJump(endJump);
    }
}

void Compiler::orExpression() {
    if (hasError) return;
    andExpression();
    while (current.type == TokenType::OR) {
        advance();
        int elseJump = emitJump(OpCode::OP_JUMP_IF_FALSE);
        int endJump = emitJump(OpCode::OP_JUMP);
        patchJump(elseJump);
        chunk().writeOp(OpCode::OP_POP);
        andExpression();
        patchJump(endJump);
    }
}

void Compiler::expression() {
    if (hasError) return;
    orExpression();
}

void Compiler::varDeclaration() {
    advance(); // consume 'let'
    if (current.type != TokenType::IDENTIFIER) {
        std::cout << "[Syntax Error]: Expected variable name after 'let'" << std::endl;
        hasError = true;
        return;
    }
    std::string varName = current.text;
    advance();
    consume(TokenType::EQUAL, "Expected '=' after variable name");
    expression();
    consume(TokenType::TILDE, "Every statement must end with '~'");

    if (currentContext->scopeDepth > 0) {
        addLocal(varName);
    } else {
        uint8_t nameIdx = chunk().addConstant(Value(varName));
        chunk().writeOp(OpCode::OP_DEFINE_GLOBAL);
        chunk().writeByte(nameIdx);
    }
}

void Compiler::taskDeclaration() {
    advance(); // consume 'task'
    if (current.type != TokenType::IDENTIFIER) {
        std::cout << "[Syntax Error]: Expected task name after 'task'" << std::endl;
        hasError = true;
        return;
    }
    std::string fnName = current.text;
    advance();

    FunctionPtr fn = std::make_shared<ObjFunction>();
    fn->name = fnName;

    CompilerContext fnContext(fn->chunk);
    fnContext.enclosing = currentContext;
    fnContext.function = fn;
    fnContext.type = FunctionType::TYPE_FUNCTION;
    fnContext.scopeDepth = 1;

    // Stack slot 0 for function instance call frame
    fnContext.locals[fnContext.localCount++].name = "";
    fnContext.locals[0].depth = 0;

    CompilerContext* parentContext = currentContext;
    Loop* enclosingLoop = currentLoop;

    {
        currentContext = &fnContext;
        currentLoop = nullptr;

        struct TaskScopeGuard {
            CompilerContext** ctxPtr;
            CompilerContext* parentCtx;
            Loop** loopPtr;
            Loop* parentLoop;
            TaskScopeGuard(CompilerContext** cP, CompilerContext* pC, Loop** lP, Loop* pL)
                : ctxPtr(cP), parentCtx(pC), loopPtr(lP), parentLoop(pL) {}
            ~TaskScopeGuard() {
                *ctxPtr = parentCtx;
                *loopPtr = parentLoop;
            }
        } taskGuard(&currentContext, parentContext, &currentLoop, enclosingLoop);

        consume(TokenType::LPAREN, "Expected '(' after task name");
        if (current.type != TokenType::RPAREN) {
            do {
                fn->arity++;
                if (fn->arity > 255) {
                    std::cout << "[Compiler Error]: Cannot have more than 255 parameters." << std::endl;
                    hasError = true;
                }
                if (current.type != TokenType::IDENTIFIER) {
                    std::cout << "[Syntax Error]: Expected parameter name" << std::endl;
                    hasError = true;
                } else {
                    addLocal(current.text);
                    advance();
                }
            } while (match(TokenType::COMMA));
        }
        consume(TokenType::RPAREN, "Expected ')' after parameters");
        consume(TokenType::LBRACE, "Expected '{' before task body");

        while (current.type != TokenType::RBRACE && current.type != TokenType::END_OF_FILE && !hasError) {
            statement();
        }
        consume(TokenType::RBRACE, "Expected '}' after task body");

        // Default implicit return nil
        chunk().writeOp(OpCode::OP_NIL);
        chunk().writeOp(OpCode::OP_RETURN);
    }

    if (hasError) return;

    uint8_t fnConstantIdx = chunk().addConstant(Value(fn));
    chunk().writeOp(OpCode::OP_CONSTANT);
    chunk().writeByte(fnConstantIdx);

    if (currentContext->scopeDepth > 0) {
        addLocal(fnName);
    } else {
        uint8_t nameIdx = chunk().addConstant(Value(fnName));
        chunk().writeOp(OpCode::OP_DEFINE_GLOBAL);
        chunk().writeByte(nameIdx);
    }
}

void Compiler::giveStatement() {
    advance(); // consume 'give'
    if (currentContext->type == FunctionType::TYPE_SCRIPT) {
        std::cout << "[Compiler Error]: Cannot give from top-level code." << std::endl;
        hasError = true;
        return;
    }

    if (current.type == TokenType::TILDE) {
        chunk().writeOp(OpCode::OP_NIL);
    } else {
        expression();
    }
    consume(TokenType::TILDE, "Every statement must end with '~'");
    chunk().writeOp(OpCode::OP_RETURN);
}

void Compiler::blockStatement() {
    advance(); // consume '{'
    beginScope();
    while (current.type != TokenType::RBRACE && current.type != TokenType::END_OF_FILE && !hasError) {
        statement();
    }
    consume(TokenType::RBRACE, "Expected '}' after block");
    endScope();
}

void Compiler::ifStatement() {
    advance(); // consume 'if'
    consume(TokenType::LPAREN, "Expected '(' after 'if'");
    expression();
    consume(TokenType::RPAREN, "Expected ')' after condition");

    int thenJump = emitJump(OpCode::OP_JUMP_IF_FALSE);
    chunk().writeOp(OpCode::OP_POP);

    statement();

    int elseJump = emitJump(OpCode::OP_JUMP);
    patchJump(thenJump);
    chunk().writeOp(OpCode::OP_POP);

    if (match(TokenType::ELSE)) {
        statement();
    }
    patchJump(elseJump);
}

void Compiler::whileStatement() {
    advance(); // consume 'while'
    Loop loop;
    loop.startIP = static_cast<int>(chunk().code.size());
    loop.scopeDepth = currentContext->scopeDepth;
    loop.enclosing = currentLoop;
    currentLoop = &loop;

    struct LoopGuard {
        Loop** targetPtr;
        Loop* resetVal;
        LoopGuard(Loop** ptr, Loop* val) : targetPtr(ptr), resetVal(val) {}
        ~LoopGuard() { *targetPtr = resetVal; }
    } loopGuard(&currentLoop, loop.enclosing);

    consume(TokenType::LPAREN, "Expected '(' after 'while'");
    expression();
    consume(TokenType::RPAREN, "Expected ')' after condition");

    int exitJump = emitJump(OpCode::OP_JUMP_IF_FALSE);
    chunk().writeOp(OpCode::OP_POP);

    statement();

    emitLoop(loop.startIP);

    patchJump(exitJump);
    chunk().writeOp(OpCode::OP_POP);

    for (int breakJump : loop.breakJumps) {
        patchJump(breakJump);
    }
}

void Compiler::haltStatement() {
    advance(); // consume 'halt'
    if (!currentLoop) {
        std::cout << "[Compiler Error]: Cannot use 'halt' outside of a loop." << std::endl;
        hasError = true;
        consume(TokenType::TILDE, "Every statement must end with '~'");
        return;
    }
    consume(TokenType::TILDE, "Every statement must end with '~'");
    for (int i = currentContext->localCount - 1; i >= 0; i--) {
        if (currentContext->locals[i].depth > currentLoop->scopeDepth) {
            chunk().writeOp(OpCode::OP_POP);
        } else {
            break;
        }
    }
    int breakJump = emitJump(OpCode::OP_JUMP);
    currentLoop->breakJumps.push_back(breakJump);
}

void Compiler::skipStatement() {
    advance(); // consume 'skip'
    if (!currentLoop) {
        std::cout << "[Compiler Error]: Cannot use 'skip' outside of a loop." << std::endl;
        hasError = true;
        consume(TokenType::TILDE, "Every statement must end with '~'");
        return;
    }
    consume(TokenType::TILDE, "Every statement must end with '~'");
    for (int i = currentContext->localCount - 1; i >= 0; i--) {
        if (currentContext->locals[i].depth > currentLoop->scopeDepth) {
            chunk().writeOp(OpCode::OP_POP);
        } else {
            break;
        }
    }
    emitLoop(currentLoop->startIP);
}

void Compiler::grabStatement() {
    advance(); // consume 'grab'
    if (current.type != TokenType::STRING) {
        std::cout << "[Syntax Error]: Expected filename string after 'grab'." << std::endl;
        hasError = true;
        return;
    }
    emitConstant(Value(current.strValue));
    advance();
    consume(TokenType::TILDE, "Every statement must end with '~'");
    chunk().writeOp(OpCode::OP_GRAB);
    chunk().writeOp(OpCode::OP_POP);
}

void Compiler::statement() {
    if (hasError) return;
    if (current.type == TokenType::LET) {
        varDeclaration();
    } else if (current.type == TokenType::TASK) {
        taskDeclaration();
    } else if (current.type == TokenType::GIVE) {
        giveStatement();
    } else if (current.type == TokenType::HALT) {
        haltStatement();
    } else if (current.type == TokenType::SKIP) {
        skipStatement();
    } else if (current.type == TokenType::GRAB) {
        grabStatement();
    } else if (current.type == TokenType::ECHO) {
        advance();
        expression();
        consume(TokenType::TILDE, "Every statement must end with '~'");
        chunk().writeOp(OpCode::OP_PRINT);
    } else if (current.type == TokenType::IF) {
        ifStatement();
    } else if (current.type == TokenType::WHILE) {
        whileStatement();
    } else if (current.type == TokenType::LBRACE) {
        blockStatement();
    } else {
        // Expression statement or assignment
        // Handle indexing assignment e.g. nums[0] = 42 ~ or variable assignment e.g. x = 42 ~
        // First compile target expression
        expression();
        if (match(TokenType::EQUAL)) {
            // Check if last opcode emitted in target expression was OP_GET_INDEX or OP_GET_GLOBAL / OP_GET_LOCAL
            if (!chunk().code.empty() && static_cast<OpCode>(chunk().code.back()) == OpCode::OP_GET_INDEX) {
                // Replace OP_GET_INDEX with assignment value compilation then OP_SET_INDEX
                chunk().code.pop_back(); // remove OP_GET_INDEX
                expression(); // value to set
                consume(TokenType::TILDE, "Every statement must end with '~'");
                chunk().writeOp(OpCode::OP_SET_INDEX);
                chunk().writeOp(OpCode::OP_POP); // pop assignment result
            } else if (chunk().code.size() >= 2 && static_cast<OpCode>(chunk().code[chunk().code.size() - 2]) == OpCode::OP_GET_LOCAL) {
                uint8_t localSlot = chunk().code.back();
                chunk().code.pop_back();
                chunk().code.pop_back();
                expression();
                consume(TokenType::TILDE, "Every statement must end with '~'");
                chunk().writeOp(OpCode::OP_SET_LOCAL);
                chunk().writeByte(localSlot);
                chunk().writeOp(OpCode::OP_POP);
            } else if (chunk().code.size() >= 2 && static_cast<OpCode>(chunk().code[chunk().code.size() - 2]) == OpCode::OP_GET_GLOBAL) {
                uint8_t nameIdx = chunk().code.back();
                chunk().code.pop_back();
                chunk().code.pop_back();
                expression();
                consume(TokenType::TILDE, "Every statement must end with '~'");
                chunk().writeOp(OpCode::OP_SET_GLOBAL);
                chunk().writeByte(nameIdx);
                chunk().writeOp(OpCode::OP_POP);
            } else {
                std::cout << "[Syntax Error]: Invalid assignment target." << std::endl;
                hasError = true;
            }
        } else {
            consume(TokenType::TILDE, "Every statement must end with '~'");
            chunk().writeOp(OpCode::OP_POP);
        }
    }
}

bool Compiler::compile() {
    CompilerContext scriptContext(targetChunk);
    currentContext = &scriptContext;

    while (current.type != TokenType::END_OF_FILE && !hasError) {
        statement();
    }
    chunk().writeOp(OpCode::OP_RETURN);
    return !hasError;
}
