#include "compiler.h"
#include <iostream>

Compiler::Compiler(const std::string& src, Chunk& targetChunk)
    : lexer(src), targetChunk(targetChunk) {
    advance();
}

void Compiler::errorAt(const Token& token, const std::string& message, const std::string& errorType) {
    if (panicMode) return;
    panicMode = true;
    hasError = true;

    std::cout << "[" << errorType << "]: " << message;
    if (token.type == TokenType::END_OF_FILE) {
        std::cout << " at end of file" << std::endl;
    } else {
        std::cout << " at line " << token.line << ", col " << token.column << " (Found: '" << token.text << "')" << std::endl;
    }

    std::string lineStr = lexer.getLineString(token.line);
    if (!lineStr.empty()) {
        std::cout << "  " << lineStr << std::endl;
        std::cout << "  ";
        int col = token.column > 1 ? token.column - 1 : 0;
        for (int i = 0; i < col; i++) {
            if (i < static_cast<int>(lineStr.size()) && lineStr[i] == '\t') {
                std::cout << "\t";
            } else {
                std::cout << " ";
            }
        }
        std::cout << "^" << std::endl;
    }
}

void Compiler::error(const std::string& message, const std::string& errorType) {
    errorAt(prev, message, errorType);
}

void Compiler::advance() {
    prev = current;
    current = lexer.nextToken();
    if (current.type == TokenType::ERROR) {
        errorAt(current, current.text, "Syntax Error");
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
        errorAt(current, errMsg, "Syntax Error");
    }
}

uint16_t Compiler::addConstant(Value value) {
    try {
        return chunk().addConstant(value);
    } catch (const std::exception& ex) {
        error(ex.what(), "Compiler Error");
        return 0;
    }
}

void Compiler::emitConstant(Value value) {
    chunk().writeOp(OpCode::OP_CONSTANT);
    uint16_t idx = addConstant(value);
    chunk().write16(idx);
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
        error("Too much code to jump over.", "Compiler Error");
        return;
    }
    chunk().code[offset] = static_cast<uint8_t>((jump >> 8) & 0xff);
    chunk().code[offset + 1] = static_cast<uint8_t>(jump & 0xff);
}

void Compiler::emitLoop(int loopStart) {
    chunk().writeOp(OpCode::OP_LOOP);
    int jump = static_cast<int>(chunk().code.size() - loopStart + 2);
    if (jump > 0xffff) {
        error("Loop body too large.", "Compiler Error");
        return;
    }
    chunk().write16(static_cast<uint16_t>(jump));
}

void Compiler::beginScope() {
    currentContext->scopeDepth++;
}

void Compiler::endScope() {
    currentContext->scopeDepth--;
    while (!currentContext->locals.empty() &&
           currentContext->locals.back().depth > currentContext->scopeDepth) {
        chunk().writeOp(OpCode::OP_CLOSE_UPVALUE);
        currentContext->locals.pop_back();
    }
}

void Compiler::addLocal(const std::string& name, bool isConst, TypeSpec typeSpec) {
    if (currentContext->locals.size() >= 65536) {
        error("Too many local variables in function (maximum 65,536).", "Compiler Error");
        return;
    }

    for (int i = static_cast<int>(currentContext->locals.size()) - 1; i >= 0; i--) {
        Local* local = &currentContext->locals[i];
        if (local->depth != -1 && local->depth < currentContext->scopeDepth) {
            break;
        }
        if (local->name == name) {
            error("Variable with name '" + name + "' already declared in this scope.", "Compiler Error");
            return;
        }
    }

    Local local;
    local.name = name;
    local.depth = currentContext->scopeDepth;
    local.isConst = isConst;
    local.typeSpec = typeSpec;
    size_t slot = currentContext->locals.size();
    currentContext->locals.push_back(local);

    if (slot >= chunk().localTypes.size()) {
        chunk().localTypes.resize(slot + 1, TypeSpec{TypeKind::ANY});
    }
    chunk().localTypes[slot] = typeSpec;
}

int Compiler::resolveLocal(CompilerContext* context, const std::string& name) {
    for (int i = static_cast<int>(context->locals.size()) - 1; i >= 0; i--) {
        if (context->locals[i].name == name) {
            return i;
        }
    }
    return -1;
}

int Compiler::resolveUpvalue(CompilerContext* context, const std::string& name) {
    if (context->enclosing == nullptr) return -1;

    int local = resolveLocal(context->enclosing, name);
    if (local != -1) {
        return addUpvalue(context, static_cast<uint16_t>(local), true, name,
                          context->enclosing->locals[local].isConst,
                          context->enclosing->locals[local].typeSpec);
    }

    int upvalue = resolveUpvalue(context->enclosing, name);
    if (upvalue != -1) {
        return addUpvalue(context, static_cast<uint16_t>(upvalue), false, name,
                          context->enclosing->upvalues[upvalue].isConst,
                          context->enclosing->upvalues[upvalue].typeSpec);
    }

    return -1;
}

int Compiler::addUpvalue(CompilerContext* context, uint16_t index, bool isLocal, const std::string& name, bool isConst, TypeSpec typeSpec) {
    int count = static_cast<int>(context->upvalues.size());
    for (int i = 0; i < count; i++) {
        Upvalue* u = &context->upvalues[i];
        if (u->index == index && u->isLocal == isLocal) {
            return i;
        }
    }
    if (count >= 65536) {
        error("Too many closure variables in function.", "Compiler Error");
        return 0;
    }
    Upvalue u;
    u.index = index;
    u.isLocal = isLocal;
    u.name = name;
    u.isConst = isConst;
    u.typeSpec = typeSpec;
    context->upvalues.push_back(u);
    context->function->upvalueCount = static_cast<int>(context->upvalues.size());
    return count;
}

uint16_t Compiler::argumentList() {
    struct ArgInfo {
        bool isNamed = false;
        std::string name;
        bool isSpread = false;
    };
    std::vector<ArgInfo> args;

    int startOffset = static_cast<int>(chunk().code.size());
    bool hasSpread = false;
    bool seenNamed = false;
    std::vector<std::string> namedArgNames;

    if (current.type != TokenType::RPAREN) {
        do {
            ArgInfo info;
            if (current.type == TokenType::IDENTIFIER && lexer.peekToken().type == TokenType::EQUAL) {
                info.isNamed = true;
                info.name = current.text;
                advance(); // consume identifier
                advance(); // consume '='
                seenNamed = true;
                namedArgNames.push_back(info.name);
            } else if (seenNamed) {
                error("Positional arguments cannot follow named arguments.", "Syntax Error");
            }

            bool isSliceArg = false;
            bool hasStart = false, hasEnd = false, hasStep = false;

            if (current.type == TokenType::COLON) {
                isSliceArg = true;
                chunk().writeOp(OpCode::OP_NIL); // omitted start
            } else {
                expression();
                hasStart = true;
            }

            if (match(TokenType::COLON)) {
                isSliceArg = true;
                if (current.type != TokenType::COLON && current.type != TokenType::COMMA && current.type != TokenType::RPAREN) {
                    expression();
                    hasEnd = true;
                } else {
                    chunk().writeOp(OpCode::OP_NIL); // omitted end
                }

                if (match(TokenType::COLON)) {
                    if (current.type != TokenType::COMMA && current.type != TokenType::RPAREN) {
                        expression();
                        hasStep = true;
                    } else {
                        chunk().writeOp(OpCode::OP_NIL); // omitted step
                    }
                } else {
                    chunk().writeOp(OpCode::OP_NIL); // omitted step
                }
            }

            if (isSliceArg) {
                chunk().writeOp(OpCode::OP_BUILD_SLICE);
                uint8_t flags = (hasStart ? 1 : 0) | (hasEnd ? 2 : 0) | (hasStep ? 4 : 0);
                chunk().writeByte(flags);
            }

            if (match(TokenType::DOT_DOT_DOT)) {
                info.isSpread = true;
                if (!hasSpread) {
                    hasSpread = true;
                    chunk().code.insert(chunk().code.begin() + startOffset, static_cast<uint8_t>(OpCode::OP_BEGIN_CALL));
                }
                chunk().writeOp(OpCode::OP_SPREAD_ARG);
            }

            args.push_back(info);
            if (args.size() == 65535) {
                error("Cannot have more than 65,535 arguments.", "Compiler Error");
            }
        } while (match(TokenType::COMMA));
    }
    consume(TokenType::RPAREN, "Expected ')' after arguments");

    uint16_t totalArgCount = static_cast<uint16_t>(args.size());
    uint16_t namedCount = static_cast<uint16_t>(namedArgNames.size());

    if (!hasSpread) {
        if (namedCount == 0) {
            chunk().writeOp(OpCode::OP_CALL);
            chunk().write16(totalArgCount);
        } else {
            chunk().writeOp(OpCode::OP_CALL_NAMED);
            chunk().write16(totalArgCount);
            chunk().write16(namedCount);
            for (const auto& name : namedArgNames) {
                uint16_t nameIdx = addConstant(Value(name));
                chunk().write16(nameIdx);
            }
        }
    } else {
        if (namedCount == 0) {
            chunk().writeOp(OpCode::OP_CALL_VAR);
        } else {
            chunk().writeOp(OpCode::OP_CALL_VAR_NAMED);
            chunk().write16(namedCount);
            for (const auto& name : namedArgNames) {
                uint16_t nameIdx = addConstant(Value(name));
                chunk().write16(nameIdx);
            }
        }
    }

    return totalArgCount;
}

TypeSpec Compiler::parseTypeDeclaration() {
    TypeSpec spec;
    if (current.type == TokenType::TYPE_INT) { spec.kind = TypeKind::INT; advance(); }
    else if (current.type == TokenType::TYPE_FLOAT) { spec.kind = TypeKind::FLOAT; advance(); }
    else if (current.type == TokenType::TYPE_STRING) { spec.kind = TypeKind::STRING; advance(); }
    else if (current.type == TokenType::TYPE_BOOL) { spec.kind = TypeKind::BOOL; advance(); }
    else if (current.type == TokenType::TYPE_CHAR) { spec.kind = TypeKind::CHAR; advance(); }
    else if (current.type == TokenType::TYPE_FUNC) { spec.kind = TypeKind::FUNC; advance(); }
    else if (current.type == TokenType::TYPE_ARRAY) {
        spec.kind = TypeKind::ARRAY;
        advance();
        if (current.type == TokenType::LESS) {
            advance(); // consume '<'
            TypeSpec elemSpec = parseTypeDeclaration();
            spec.elementKind = elemSpec.kind;
            spec.elemType = std::make_shared<TypeSpec>(elemSpec);
            consume(TokenType::GREATER, "Expected '>' after array element type");
        }
    } else if (current.type == TokenType::TYPE_MAP) {
        spec.kind = TypeKind::MAP;
        advance();
        if (current.type == TokenType::LESS) {
            advance(); // consume '<'
            TypeSpec kSpec = parseTypeDeclaration();
            consume(TokenType::COMMA, "Expected ',' between map key and value types");
            TypeSpec vSpec = parseTypeDeclaration();
            spec.keyKind = kSpec.kind;
            spec.valueKind = vSpec.kind;
            spec.keyType = std::make_shared<TypeSpec>(kSpec);
            spec.valType = std::make_shared<TypeSpec>(vSpec);
            consume(TokenType::GREATER, "Expected '>' after map value type");
        }
    }
    return spec;
}

void Compiler::primary() {
    if (hasError) return;
    if (current.type == TokenType::INT_LITERAL) {
        emitConstant(Value(current.intValue));
        advance();
    } else if (current.type == TokenType::FLOAT_LITERAL) {
        emitConstant(Value(current.floatValue));
        advance();
    } else if (current.type == TokenType::CHAR_LITERAL) {
        emitConstant(Value(current.charValue, true));
        advance();
    } else if (current.type == TokenType::STRING_LITERAL) {
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
            chunk().write16(static_cast<uint16_t>(localSlot));
        } else {
            int upvalueSlot = resolveUpvalue(currentContext, name);
            if (upvalueSlot != -1) {
                chunk().writeOp(OpCode::OP_GET_UPVALUE);
                chunk().write16(static_cast<uint16_t>(upvalueSlot));
            } else {
                uint16_t nameIdx = addConstant(Value(name));
                chunk().writeOp(OpCode::OP_GET_GLOBAL);
                chunk().write16(nameIdx);
            }
        }
    } else if (current.type == TokenType::LPAREN) {
        advance();
        expression();
        consume(TokenType::RPAREN, "Expected ')' after expression");
    } else if (current.type == TokenType::LBRACKET) {
        advance();
        uint16_t elementCount = 0;
        if (current.type != TokenType::RBRACKET) {
            do {
                expression();
                if (elementCount == 65535) {
                    error("Cannot have more than 65,535 elements in array literal.", "Compiler Error");
                }
                elementCount++;
            } while (match(TokenType::COMMA));
        }
        consume(TokenType::RBRACKET, "Expected ']' after array elements");
        chunk().writeOp(OpCode::OP_BUILD_ARRAY);
        chunk().write16(elementCount);
    } else if (current.type == TokenType::LBRACE) {
        advance();
        uint16_t entryCount = 0;
        if (current.type != TokenType::RBRACE) {
            do {
                expression();
                consume(TokenType::COLON, "Expected ':' after map key");
                expression();
                if (entryCount == 65535) {
                    error("Cannot have more than 65,535 entries in map literal.", "Compiler Error");
                }
                entryCount++;
            } while (match(TokenType::COMMA));
        }
        consume(TokenType::RBRACE, "Expected '}' after map entries");
        chunk().writeOp(OpCode::OP_BUILD_MAP);
        chunk().write16(entryCount);
    } else if (current.type == TokenType::TASK) {
        advance(); // consume 'task'
        std::string fnName = "";
        if (current.type == TokenType::IDENTIFIER) {
            fnName = current.text;
            advance();
        }

        FunctionPtr fn = std::make_shared<ObjFunction>();
        fn->name = fnName;

        CompilerContext fnContext(fn->chunk);
        fnContext.enclosing = currentContext;
        fnContext.function = fn;
        fnContext.type = FunctionType::TYPE_FUNCTION;
        fnContext.scopeDepth = 1;

        Local slot0;
        slot0.name = "";
        slot0.depth = 0;
        fnContext.locals.push_back(slot0);

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

            consume(TokenType::LPAREN, "Expected '(' after 'task'");
            if (current.type != TokenType::RPAREN) {
                do {
                    fn->arity++;
                    if (fn->arity > 65535) {
                        error("Cannot have more than 65,535 parameters.", "Compiler Error");
                    }
                    if (current.type == TokenType::PUB || current.type == TokenType::PRIV) {
                        error("Visibility modifiers ('pub'/'priv') are not allowed on task parameters.", "Compiler Error");
                    }
                    TypeSpec pSpec;
                    if (current.type == TokenType::TYPE_INT || current.type == TokenType::TYPE_FLOAT ||
                        current.type == TokenType::TYPE_STRING || current.type == TokenType::TYPE_BOOL ||
                        current.type == TokenType::TYPE_CHAR || current.type == TokenType::TYPE_ARRAY ||
                        current.type == TokenType::TYPE_MAP || current.type == TokenType::TYPE_FUNC) {
                        pSpec = parseTypeDeclaration();
                    }
                    fn->paramTypes.push_back(pSpec);

                    if (current.type != TokenType::IDENTIFIER) {
                        errorAt(current, "Expected parameter name", "Syntax Error");
                    } else {
                        fn->paramNames.push_back(current.text);
                        addLocal(current.text, false, pSpec);
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

            chunk().writeOp(OpCode::OP_NIL);
            chunk().writeOp(OpCode::OP_RETURN);

            fn->localTypes = fn->chunk.localTypes;
        }

        if (hasError) return;

        uint16_t fnConstantIdx = addConstant(Value(fn));
        chunk().writeOp(OpCode::OP_CLOSURE);
        chunk().write16(fnConstantIdx);

        for (size_t i = 0; i < fnContext.upvalues.size(); i++) {
            chunk().writeByte(fnContext.upvalues[i].isLocal ? 1 : 0);
            chunk().write16(fnContext.upvalues[i].index);
        }
    } else {
        errorAt(current, "Expected expression", "Syntax Error");
    }
}

void Compiler::postfix() {
    primary();
    while (!hasError) {
        if (match(TokenType::LPAREN)) {
            argumentList();
        } else if (match(TokenType::LBRACKET)) {
            bool isSlice = false;
            bool hasStart = false;
            bool hasEnd = false;
            bool hasStep = false;

            if (current.type == TokenType::COLON) {
                isSlice = true;
                chunk().writeOp(OpCode::OP_NIL); // omitted start
            } else {
                expression();
                hasStart = true;
            }

            if (match(TokenType::COLON)) {
                isSlice = true;
                if (current.type != TokenType::COLON && current.type != TokenType::RBRACKET) {
                    expression();
                    hasEnd = true;
                } else {
                    chunk().writeOp(OpCode::OP_NIL); // omitted end
                }

                if (match(TokenType::COLON)) {
                    if (current.type != TokenType::RBRACKET) {
                        expression();
                        hasStep = true;
                    } else {
                        chunk().writeOp(OpCode::OP_NIL); // omitted step
                    }
                } else {
                    chunk().writeOp(OpCode::OP_NIL); // omitted step
                }
            }

            consume(TokenType::RBRACKET, "Expected ']' after index or slice");

            if (isSlice) {
                chunk().writeOp(OpCode::OP_BUILD_SLICE);
                uint8_t flags = (hasStart ? 1 : 0) | (hasEnd ? 2 : 0) | (hasStep ? 4 : 0);
                chunk().writeByte(flags);
            }
            chunk().writeOp(OpCode::OP_GET_INDEX);
        } else if (match(TokenType::DOT)) {
            if (current.type != TokenType::IDENTIFIER &&
                current.type != TokenType::TYPE_INT &&
                current.type != TokenType::TYPE_FLOAT &&
                current.type != TokenType::TYPE_STRING &&
                current.type != TokenType::TYPE_BOOL &&
                current.type != TokenType::TYPE_CHAR &&
                current.type != TokenType::TYPE_ARRAY &&
                current.type != TokenType::TYPE_MAP &&
                current.type != TokenType::TYPE_FUNC) {
                errorAt(current, "Expected member name after '.'.", "Syntax Error");
                return;
            }
            std::string memberName = current.text;
            advance();
            uint16_t nameIdx = addConstant(Value(memberName));
            chunk().writeOp(OpCode::OP_GET_MEMBER);
            chunk().write16(nameIdx);
        } else if (match(TokenType::PLUS_PLUS) || match(TokenType::MINUS_MINUS)) {
            OpCode incOp = (prev.type == TokenType::PLUS_PLUS) ? OpCode::OP_INC : OpCode::OP_DEC;
            std::string opStr = (prev.type == TokenType::PLUS_PLUS) ? "++" : "--";

            if (!chunk().code.empty() && static_cast<OpCode>(chunk().code.back()) == OpCode::OP_GET_INDEX) {
                chunk().code.pop_back(); // remove OP_GET_INDEX
                chunk().writeOp(OpCode::OP_DUP_2);
                chunk().writeOp(OpCode::OP_GET_INDEX);
                chunk().writeOp(incOp);
                chunk().writeOp(OpCode::OP_SET_INDEX_POST);
            } else if (chunk().code.size() >= 3 && static_cast<OpCode>(chunk().code[chunk().code.size() - 3]) == OpCode::OP_GET_MEMBER) {
                uint16_t memberIdx = (static_cast<uint16_t>(chunk().code[chunk().code.size() - 2]) << 8) | chunk().code.back();
                chunk().code.pop_back(); chunk().code.pop_back(); chunk().code.pop_back();
                chunk().writeOp(OpCode::OP_DUP);
                chunk().writeOp(OpCode::OP_GET_MEMBER);
                chunk().write16(memberIdx);
                chunk().writeOp(incOp);
                chunk().writeOp(OpCode::OP_SET_MEMBER_POST);
                chunk().write16(memberIdx);
            } else if (chunk().code.size() >= 3 && static_cast<OpCode>(chunk().code[chunk().code.size() - 3]) == OpCode::OP_GET_LOCAL) {
                uint16_t localSlot = (static_cast<uint16_t>(chunk().code[chunk().code.size() - 2]) << 8) | chunk().code.back();
                chunk().code.pop_back(); chunk().code.pop_back(); chunk().code.pop_back();
                if (localSlot < currentContext->locals.size() && currentContext->locals[localSlot].isConst) {
                    error("Cannot reassign constant variable '" + currentContext->locals[localSlot].name + "'.", "Compiler Error");
                }
                chunk().writeOp(OpCode::OP_GET_LOCAL);
                chunk().write16(localSlot);
                chunk().writeOp(OpCode::OP_DUP);
                chunk().writeOp(incOp);
                chunk().writeOp(OpCode::OP_SET_LOCAL);
                chunk().write16(localSlot);
                chunk().writeOp(OpCode::OP_POP);
            } else if (chunk().code.size() >= 3 && static_cast<OpCode>(chunk().code[chunk().code.size() - 3]) == OpCode::OP_GET_UPVALUE) {
                uint16_t upvalueSlot = (static_cast<uint16_t>(chunk().code[chunk().code.size() - 2]) << 8) | chunk().code.back();
                chunk().code.pop_back(); chunk().code.pop_back(); chunk().code.pop_back();
                if (upvalueSlot < currentContext->upvalues.size() && currentContext->upvalues[upvalueSlot].isConst) {
                    error("Cannot reassign constant variable '" + currentContext->upvalues[upvalueSlot].name + "'.", "Compiler Error");
                }
                chunk().writeOp(OpCode::OP_GET_UPVALUE);
                chunk().write16(upvalueSlot);
                chunk().writeOp(OpCode::OP_DUP);
                chunk().writeOp(incOp);
                chunk().writeOp(OpCode::OP_SET_UPVALUE);
                chunk().write16(upvalueSlot);
                chunk().writeOp(OpCode::OP_POP);
            } else if (chunk().code.size() >= 3 && static_cast<OpCode>(chunk().code[chunk().code.size() - 3]) == OpCode::OP_GET_GLOBAL) {
                uint16_t nameIdx = (static_cast<uint16_t>(chunk().code[chunk().code.size() - 2]) << 8) | chunk().code.back();
                chunk().code.pop_back(); chunk().code.pop_back(); chunk().code.pop_back();
                std::string varName = chunk().constants[nameIdx].str;
                if (globalConsts.find(varName) != globalConsts.end() && globalConsts[varName]) {
                    error("Cannot reassign constant variable '" + varName + "'.", "Compiler Error");
                }
                chunk().writeOp(OpCode::OP_GET_GLOBAL);
                chunk().write16(nameIdx);
                chunk().writeOp(OpCode::OP_DUP);
                chunk().writeOp(incOp);
                chunk().writeOp(OpCode::OP_SET_GLOBAL);
                chunk().write16(nameIdx);
                chunk().writeOp(OpCode::OP_POP);
            } else {
                error("Invalid operand for '" + opStr + "'.", "Compiler Error");
            }
        } else {
            break;
        }
    }
}

void Compiler::unary() {
    if (hasError) return;
    if (current.type == TokenType::PLUS_PLUS || current.type == TokenType::MINUS_MINUS) {
        OpCode incOp = (current.type == TokenType::PLUS_PLUS) ? OpCode::OP_INC : OpCode::OP_DEC;
        std::string opStr = (current.type == TokenType::PLUS_PLUS) ? "++" : "--";
        advance();
        postfix();
        if (!chunk().code.empty() && static_cast<OpCode>(chunk().code.back()) == OpCode::OP_GET_INDEX) {
            chunk().code.pop_back();
            chunk().writeOp(OpCode::OP_DUP_2);
            chunk().writeOp(OpCode::OP_GET_INDEX);
            chunk().writeOp(incOp);
            chunk().writeOp(OpCode::OP_SET_INDEX);
        } else if (chunk().code.size() >= 3 && static_cast<OpCode>(chunk().code[chunk().code.size() - 3]) == OpCode::OP_GET_MEMBER) {
            uint16_t memberIdx = (static_cast<uint16_t>(chunk().code[chunk().code.size() - 2]) << 8) | chunk().code.back();
            chunk().code.pop_back(); chunk().code.pop_back(); chunk().code.pop_back();
            chunk().writeOp(OpCode::OP_DUP);
            chunk().writeOp(OpCode::OP_GET_MEMBER);
            chunk().write16(memberIdx);
            chunk().writeOp(incOp);
            chunk().writeOp(OpCode::OP_SET_MEMBER);
            chunk().write16(memberIdx);
        } else if (chunk().code.size() >= 3 && static_cast<OpCode>(chunk().code[chunk().code.size() - 3]) == OpCode::OP_GET_LOCAL) {
            uint16_t localSlot = (static_cast<uint16_t>(chunk().code[chunk().code.size() - 2]) << 8) | chunk().code.back();
            chunk().code.pop_back(); chunk().code.pop_back(); chunk().code.pop_back();
            if (localSlot < currentContext->locals.size() && currentContext->locals[localSlot].isConst) {
                error("Cannot reassign constant variable '" + currentContext->locals[localSlot].name + "'.", "Compiler Error");
            }
            chunk().writeOp(OpCode::OP_GET_LOCAL);
            chunk().write16(localSlot);
            chunk().writeOp(incOp);
            chunk().writeOp(OpCode::OP_SET_LOCAL);
            chunk().write16(localSlot);
        } else if (chunk().code.size() >= 3 && static_cast<OpCode>(chunk().code[chunk().code.size() - 3]) == OpCode::OP_GET_UPVALUE) {
            uint16_t upvalueSlot = (static_cast<uint16_t>(chunk().code[chunk().code.size() - 2]) << 8) | chunk().code.back();
            chunk().code.pop_back(); chunk().code.pop_back(); chunk().code.pop_back();
            if (upvalueSlot < currentContext->upvalues.size() && currentContext->upvalues[upvalueSlot].isConst) {
                error("Cannot reassign constant variable '" + currentContext->upvalues[upvalueSlot].name + "'.", "Compiler Error");
            }
            chunk().writeOp(OpCode::OP_GET_UPVALUE);
            chunk().write16(upvalueSlot);
            chunk().writeOp(incOp);
            chunk().writeOp(OpCode::OP_SET_UPVALUE);
            chunk().write16(upvalueSlot);
        } else if (chunk().code.size() >= 3 && static_cast<OpCode>(chunk().code[chunk().code.size() - 3]) == OpCode::OP_GET_GLOBAL) {
            uint16_t nameIdx = (static_cast<uint16_t>(chunk().code[chunk().code.size() - 2]) << 8) | chunk().code.back();
            chunk().code.pop_back(); chunk().code.pop_back(); chunk().code.pop_back();
            std::string varName = chunk().constants[nameIdx].str;
            if (globalConsts.find(varName) != globalConsts.end() && globalConsts[varName]) {
                error("Cannot reassign constant variable '" + varName + "'.", "Compiler Error");
            }
            chunk().writeOp(OpCode::OP_GET_GLOBAL);
            chunk().write16(nameIdx);
            chunk().writeOp(incOp);
            chunk().writeOp(OpCode::OP_SET_GLOBAL);
            chunk().write16(nameIdx);
        } else {
            error("Invalid operand for '" + opStr + "'.", "Compiler Error");
        }
    } else if (current.type == TokenType::BANG || current.type == TokenType::NOT) {
        advance();
        unary();
        chunk().writeOp(OpCode::OP_NOT);
    } else if (current.type == TokenType::MINUS) {
        advance();
        unary();
        emitConstant(Value(static_cast<int64_t>(-1)));
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

void Compiler::varDeclaration(bool isPublic) {
    bool isConst = match(TokenType::CONST);
    if (!isConst) {
        consume(TokenType::LET, "Expected 'let' or 'const' in variable declaration");
    }

    TypeSpec typeSpec;
    if (current.type == TokenType::TYPE_INT || current.type == TokenType::TYPE_FLOAT ||
        current.type == TokenType::TYPE_STRING || current.type == TokenType::TYPE_BOOL ||
        current.type == TokenType::TYPE_CHAR || current.type == TokenType::TYPE_ARRAY ||
        current.type == TokenType::TYPE_MAP || current.type == TokenType::TYPE_FUNC) {
        typeSpec = parseTypeDeclaration();
    }

    if (current.type != TokenType::IDENTIFIER) {
        errorAt(current, "Expected variable name", "Syntax Error");
        return;
    }
    std::string varName = current.text;
    advance();

    consume(TokenType::EQUAL, "Expected '=' after variable name");
    expression();
    consume(TokenType::TILDE, "Every statement must end with '~'");

    if (currentContext->scopeDepth > 0) {
        addLocal(varName, isConst, typeSpec);
        if (typeSpec.kind != TypeKind::ANY && typeSpec.kind != TypeKind::UNTYPED) {
            chunk().writeOp(OpCode::OP_CHECK_LOCAL_TYPE);
            uint16_t typeSpecIdx = addConstant(Value(typeSpec.toString()));
            chunk().write16(typeSpecIdx);
        }
    } else {
        if (isConst) {
            globalConsts[varName] = true;
        }
        uint16_t nameIdx = addConstant(Value(varName));
        uint8_t flags = (isPublic ? 1 : 0) | (isConst ? 2 : 0);
        if (typeSpec.kind != TypeKind::ANY && typeSpec.kind != TypeKind::UNTYPED) {
            chunk().writeOp(OpCode::OP_DEFINE_GLOBAL_TYPED);
            chunk().write16(nameIdx);
            uint16_t typeSpecIdx = addConstant(Value(typeSpec.toString()));
            chunk().write16(typeSpecIdx);
            chunk().writeByte(flags);
        } else {
            chunk().writeOp(OpCode::OP_DEFINE_GLOBAL);
            chunk().write16(nameIdx);
            chunk().writeByte(flags);
        }
    }
}

void Compiler::taskDeclaration(bool isPublic) {
    advance(); // consume 'task'
    if (current.type != TokenType::IDENTIFIER) {
        errorAt(current, "Expected task name after 'task'", "Syntax Error");
        return;
    }
    std::string fnName = current.text;
    advance();

    if (currentContext->scopeDepth > 0) {
        addLocal(fnName, false, TypeSpec{TypeKind::ANY});
    }

    FunctionPtr fn = std::make_shared<ObjFunction>();
    fn->name = fnName;

    CompilerContext fnContext(fn->chunk);
    fnContext.enclosing = currentContext;
    fnContext.function = fn;
    fnContext.type = FunctionType::TYPE_FUNCTION;
    fnContext.scopeDepth = 1;

    // Stack slot 0 for function instance call frame
    Local slot0;
    slot0.name = "";
    slot0.depth = 0;
    fnContext.locals.push_back(slot0);

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
                if (fn->arity > 65535) {
                    error("Cannot have more than 65,535 parameters.", "Compiler Error");
                }
                if (current.type == TokenType::PUB || current.type == TokenType::PRIV) {
                    error("Visibility modifiers ('pub'/'priv') are not allowed on task parameters.", "Compiler Error");
                }
                TypeSpec pSpec;
                if (current.type == TokenType::TYPE_INT || current.type == TokenType::TYPE_FLOAT ||
                    current.type == TokenType::TYPE_STRING || current.type == TokenType::TYPE_BOOL ||
                    current.type == TokenType::TYPE_CHAR || current.type == TokenType::TYPE_ARRAY ||
                    current.type == TokenType::TYPE_MAP || current.type == TokenType::TYPE_FUNC) {
                    pSpec = parseTypeDeclaration();
                }
                fn->paramTypes.push_back(pSpec);

                if (current.type != TokenType::IDENTIFIER) {
                    errorAt(current, "Expected parameter name", "Syntax Error");
                } else {
                    fn->paramNames.push_back(current.text);
                    addLocal(current.text, false, pSpec);
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

        // Implicit default return nil
        chunk().writeOp(OpCode::OP_NIL);
        chunk().writeOp(OpCode::OP_RETURN);

        fn->localTypes = fn->chunk.localTypes;
    }

    if (hasError) return;

    uint16_t fnConstantIdx = addConstant(Value(fn));
    chunk().writeOp(OpCode::OP_CLOSURE);
    chunk().write16(fnConstantIdx);

    for (size_t i = 0; i < fnContext.upvalues.size(); i++) {
        chunk().writeByte(fnContext.upvalues[i].isLocal ? 1 : 0);
        chunk().write16(fnContext.upvalues[i].index);
    }

    if (currentContext->scopeDepth == 0) {
        uint16_t nameIdx = addConstant(Value(fnName));
        uint8_t flags = isPublic ? 1 : 0;
        chunk().writeOp(OpCode::OP_DEFINE_GLOBAL);
        chunk().write16(nameIdx);
        chunk().writeByte(flags);
    }
}

void Compiler::giveStatement() {
    advance(); // consume 'give'
    if (currentContext->type == FunctionType::TYPE_SCRIPT) {
        error("Cannot give from top-level code.", "Compiler Error");
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

    std::vector<int> elseJumps;
    elseJumps.push_back(emitJump(OpCode::OP_JUMP));

    patchJump(thenJump);
    chunk().writeOp(OpCode::OP_POP);

    // Support native else if chain
    while (match(TokenType::ELSE)) {
        if (match(TokenType::IF)) {
            consume(TokenType::LPAREN, "Expected '(' after 'else if'");
            expression();
            consume(TokenType::RPAREN, "Expected ')' after condition");

            int nextJump = emitJump(OpCode::OP_JUMP_IF_FALSE);
            chunk().writeOp(OpCode::OP_POP);

            statement();

            elseJumps.push_back(emitJump(OpCode::OP_JUMP));

            patchJump(nextJump);
            chunk().writeOp(OpCode::OP_POP);
        } else {
            statement();
            break;
        }
    }

    for (int j : elseJumps) {
        patchJump(j);
    }
}

void Compiler::whileStatement() {
    advance(); // consume 'while'
    Loop loop;
    loop.startIP = static_cast<int>(chunk().code.size());
    loop.scopeDepth = currentContext->scopeDepth;
    loop.continueIP = loop.startIP;
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
        error("Cannot use 'halt' outside of a loop.", "Compiler Error");
        consume(TokenType::TILDE, "Every statement must end with '~'");
        return;
    }
    consume(TokenType::TILDE, "Every statement must end with '~'");

    int popsCount = 0;
    for (int i = static_cast<int>(currentContext->locals.size()) - 1; i >= 0; i--) {
        if (currentContext->locals[i].depth > currentLoop->scopeDepth) {
            popsCount++;
        } else {
            break;
        }
    }
    for (int i = 0; i < popsCount; i++) {
        chunk().writeOp(OpCode::OP_POP);
    }

    int breakJump = emitJump(OpCode::OP_JUMP);
    currentLoop->breakJumps.push_back(breakJump);
}

void Compiler::skipStatement() {
    advance(); // consume 'skip'
    if (!currentLoop) {
        error("Cannot use 'skip' outside of a loop.", "Compiler Error");
        consume(TokenType::TILDE, "Every statement must end with '~'");
        return;
    }
    consume(TokenType::TILDE, "Every statement must end with '~'");

    int popsCount = 0;
    for (int i = static_cast<int>(currentContext->locals.size()) - 1; i >= 0; i--) {
        if (currentContext->locals[i].depth > currentLoop->scopeDepth) {
            popsCount++;
        } else {
            break;
        }
    }
    for (int i = 0; i < popsCount; i++) {
        chunk().writeOp(OpCode::OP_POP);
    }

    if (currentLoop->continueIP != -1) {
        emitLoop(currentLoop->continueIP);
    } else {
        int continueJump = emitJump(OpCode::OP_JUMP);
        currentLoop->continueJumps.push_back(continueJump);
    }
}

void Compiler::grabStatement() {
    advance(); // consume 'grab'
    if (currentContext->scopeDepth > 0) {
        error("'grab' is allowed only at top-level module scope.", "Compiler Error");
        return;
    }

    if (current.type != TokenType::IDENTIFIER) {
        errorAt(current, "Expected module path after 'grab'.", "Syntax Error");
        return;
    }

    std::string pathStr = current.text;
    advance();

    while (match(TokenType::DOT)) {
        if (current.type != TokenType::IDENTIFIER) {
            errorAt(current, "Expected identifier after '.' in module path.", "Syntax Error");
            return;
        }
        pathStr += "." + current.text;
        advance();
    }

    std::string alias = "";
    if (match(TokenType::AS)) {
        if (current.type != TokenType::IDENTIFIER) {
            errorAt(current, "Expected alias identifier after 'as'.", "Syntax Error");
            return;
        }
        alias = current.text;
        advance();
    }

    consume(TokenType::TILDE, "Every statement must end with '~'");

    uint16_t pathIdx = addConstant(Value(pathStr));
    uint16_t aliasIdx = addConstant(Value(alias));
    chunk().writeOp(OpCode::OP_GRAB);
    chunk().write16(pathIdx);
    chunk().write16(aliasIdx);
}

void Compiler::statement() {
    if (hasError) return;
    if (current.type == TokenType::PUB || current.type == TokenType::PRIV) {
        bool isPublic = (current.type == TokenType::PUB);
        std::string modName = current.text;
        advance();
        if (currentContext->scopeDepth > 0) {
            error("'" + modName + "' modifier is allowed only on module-level declarations.", "Compiler Error");
            return;
        }
        if (current.type == TokenType::LET || current.type == TokenType::CONST) {
            varDeclaration(isPublic);
        } else if (current.type == TokenType::TASK) {
            taskDeclaration(isPublic);
        } else {
            errorAt(current, "Expected variable or task declaration after '" + modName + "' modifier.", "Syntax Error");
        }
    } else if (current.type == TokenType::LET || current.type == TokenType::CONST) {
        varDeclaration(false);
    } else if (current.type == TokenType::TASK) {
        taskDeclaration(false);
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
    } else if (current.type == TokenType::FOR) {
        // Native For Loop Implementation
        advance(); // consume 'for'
        beginScope();
        consume(TokenType::LPAREN, "Expected '(' after 'for'");

        // 1. Initializer
        if (match(TokenType::TILDE)) {
            // No initializer
        } else if (current.type == TokenType::LET || current.type == TokenType::CONST) {
            varDeclaration();
        } else {
            expression();
            consume(TokenType::TILDE, "Expected '~' after loop initializer");
            chunk().writeOp(OpCode::OP_POP);
        }

        Loop loop;
        loop.scopeDepth = currentContext->scopeDepth;
        loop.startIP = static_cast<int>(chunk().code.size());
        loop.enclosing = currentLoop;
        currentLoop = &loop;

        struct LoopGuard {
            Loop** targetPtr;
            Loop* resetVal;
            LoopGuard(Loop** ptr, Loop* val) : targetPtr(ptr), resetVal(val) {}
            ~LoopGuard() { *targetPtr = resetVal; }
        } loopGuard(&currentLoop, loop.enclosing);

        // 2. Condition
        int exitJump = -1;
        if (!match(TokenType::TILDE)) {
            expression();
            consume(TokenType::TILDE, "Expected '~' after loop condition");

            exitJump = emitJump(OpCode::OP_JUMP_IF_FALSE);
            chunk().writeOp(OpCode::OP_POP);
        }

        // 3. Increment clause
        if (!match(TokenType::RPAREN)) {
            int bodyJump = emitJump(OpCode::OP_JUMP);
            int incrementStart = static_cast<int>(chunk().code.size());
            loop.continueIP = incrementStart;

            for (int cJump : loop.continueJumps) {
                patchJump(cJump);
            }
            loop.continueJumps.clear();

            // Expression or assignment statement for increment
            expression();
            if (current.type == TokenType::EQUAL || current.type == TokenType::PLUS_EQUAL ||
                current.type == TokenType::MINUS_EQUAL || current.type == TokenType::STAR_EQUAL ||
                current.type == TokenType::SLASH_EQUAL || current.type == TokenType::PERCENT_EQUAL) {
                TokenType assignOp = current.type;
                advance();

                if (!chunk().code.empty() && static_cast<OpCode>(chunk().code.back()) == OpCode::OP_GET_INDEX) {
                    chunk().code.pop_back();
                    if (assignOp != TokenType::EQUAL) {
                        chunk().writeOp(OpCode::OP_DUP_2);
                        chunk().writeOp(OpCode::OP_GET_INDEX);
                        expression();
                        if (assignOp == TokenType::PLUS_EQUAL) chunk().writeOp(OpCode::OP_ADD);
                        else if (assignOp == TokenType::MINUS_EQUAL) chunk().writeOp(OpCode::OP_SUBTRACT);
                        else if (assignOp == TokenType::STAR_EQUAL) chunk().writeOp(OpCode::OP_MULTIPLY);
                        else if (assignOp == TokenType::SLASH_EQUAL) chunk().writeOp(OpCode::OP_DIVIDE);
                        else if (assignOp == TokenType::PERCENT_EQUAL) chunk().writeOp(OpCode::OP_MODULO);
                    } else {
                        expression();
                    }
                    chunk().writeOp(OpCode::OP_SET_INDEX);
                    chunk().writeOp(OpCode::OP_POP);
                } else if (chunk().code.size() >= 3 && static_cast<OpCode>(chunk().code[chunk().code.size() - 3]) == OpCode::OP_GET_MEMBER) {
                    uint16_t memberIdx = (static_cast<uint16_t>(chunk().code[chunk().code.size() - 2]) << 8) | chunk().code.back();
                    chunk().code.pop_back(); chunk().code.pop_back(); chunk().code.pop_back();
                    if (assignOp != TokenType::EQUAL) {
                        chunk().writeOp(OpCode::OP_DUP);
                        chunk().writeOp(OpCode::OP_GET_MEMBER);
                        chunk().write16(memberIdx);
                        expression();
                        if (assignOp == TokenType::PLUS_EQUAL) chunk().writeOp(OpCode::OP_ADD);
                        else if (assignOp == TokenType::MINUS_EQUAL) chunk().writeOp(OpCode::OP_SUBTRACT);
                        else if (assignOp == TokenType::STAR_EQUAL) chunk().writeOp(OpCode::OP_MULTIPLY);
                        else if (assignOp == TokenType::SLASH_EQUAL) chunk().writeOp(OpCode::OP_DIVIDE);
                        else if (assignOp == TokenType::PERCENT_EQUAL) chunk().writeOp(OpCode::OP_MODULO);
                    } else {
                        expression();
                    }
                    chunk().writeOp(OpCode::OP_SET_MEMBER);
                    chunk().write16(memberIdx);
                    chunk().writeOp(OpCode::OP_POP);
                } else if (chunk().code.size() >= 3 && static_cast<OpCode>(chunk().code[chunk().code.size() - 3]) == OpCode::OP_GET_LOCAL) {
                    uint16_t localSlot = (static_cast<uint16_t>(chunk().code[chunk().code.size() - 2]) << 8) | chunk().code.back();
                    chunk().code.pop_back(); chunk().code.pop_back(); chunk().code.pop_back();

                    if (localSlot < currentContext->locals.size() && currentContext->locals[localSlot].isConst) {
                        error("Cannot reassign constant variable '" + currentContext->locals[localSlot].name + "'.", "Compiler Error");
                    }

                    if (assignOp != TokenType::EQUAL) {
                        chunk().writeOp(OpCode::OP_GET_LOCAL);
                        chunk().write16(localSlot);
                        expression();
                        if (assignOp == TokenType::PLUS_EQUAL) chunk().writeOp(OpCode::OP_ADD);
                        else if (assignOp == TokenType::MINUS_EQUAL) chunk().writeOp(OpCode::OP_SUBTRACT);
                        else if (assignOp == TokenType::STAR_EQUAL) chunk().writeOp(OpCode::OP_MULTIPLY);
                        else if (assignOp == TokenType::SLASH_EQUAL) chunk().writeOp(OpCode::OP_DIVIDE);
                        else if (assignOp == TokenType::PERCENT_EQUAL) chunk().writeOp(OpCode::OP_MODULO);
                    } else {
                        expression();
                    }
                    chunk().writeOp(OpCode::OP_SET_LOCAL);
                    chunk().write16(localSlot);
                    chunk().writeOp(OpCode::OP_POP);
                } else if (chunk().code.size() >= 3 && static_cast<OpCode>(chunk().code[chunk().code.size() - 3]) == OpCode::OP_GET_UPVALUE) {
                    uint16_t upvalueSlot = (static_cast<uint16_t>(chunk().code[chunk().code.size() - 2]) << 8) | chunk().code.back();
                    chunk().code.pop_back(); chunk().code.pop_back(); chunk().code.pop_back();

                    if (upvalueSlot < currentContext->upvalues.size() && currentContext->upvalues[upvalueSlot].isConst) {
                        error("Cannot reassign constant variable '" + currentContext->upvalues[upvalueSlot].name + "'.", "Compiler Error");
                    }

                    if (assignOp != TokenType::EQUAL) {
                        chunk().writeOp(OpCode::OP_GET_UPVALUE);
                        chunk().write16(upvalueSlot);
                        expression();
                        if (assignOp == TokenType::PLUS_EQUAL) chunk().writeOp(OpCode::OP_ADD);
                        else if (assignOp == TokenType::MINUS_EQUAL) chunk().writeOp(OpCode::OP_SUBTRACT);
                        else if (assignOp == TokenType::STAR_EQUAL) chunk().writeOp(OpCode::OP_MULTIPLY);
                        else if (assignOp == TokenType::SLASH_EQUAL) chunk().writeOp(OpCode::OP_DIVIDE);
                        else if (assignOp == TokenType::PERCENT_EQUAL) chunk().writeOp(OpCode::OP_MODULO);
                    } else {
                        expression();
                    }
                    chunk().writeOp(OpCode::OP_SET_UPVALUE);
                    chunk().write16(upvalueSlot);
                    chunk().writeOp(OpCode::OP_POP);
                } else if (chunk().code.size() >= 3 && static_cast<OpCode>(chunk().code[chunk().code.size() - 3]) == OpCode::OP_GET_GLOBAL) {
                    uint16_t nameIdx = (static_cast<uint16_t>(chunk().code[chunk().code.size() - 2]) << 8) | chunk().code.back();
                    chunk().code.pop_back(); chunk().code.pop_back(); chunk().code.pop_back();

                    std::string varName = chunk().constants[nameIdx].str;
                    if (globalConsts.find(varName) != globalConsts.end() && globalConsts[varName]) {
                        error("Cannot reassign constant variable '" + varName + "'.", "Compiler Error");
                    }

                    if (assignOp != TokenType::EQUAL) {
                        chunk().writeOp(OpCode::OP_GET_GLOBAL);
                        chunk().write16(nameIdx);
                        expression();
                        if (assignOp == TokenType::PLUS_EQUAL) chunk().writeOp(OpCode::OP_ADD);
                        else if (assignOp == TokenType::MINUS_EQUAL) chunk().writeOp(OpCode::OP_SUBTRACT);
                        else if (assignOp == TokenType::STAR_EQUAL) chunk().writeOp(OpCode::OP_MULTIPLY);
                        else if (assignOp == TokenType::SLASH_EQUAL) chunk().writeOp(OpCode::OP_DIVIDE);
                        else if (assignOp == TokenType::PERCENT_EQUAL) chunk().writeOp(OpCode::OP_MODULO);
                    } else {
                        expression();
                    }
                    chunk().writeOp(OpCode::OP_SET_GLOBAL);
                    chunk().write16(nameIdx);
                    chunk().writeOp(OpCode::OP_POP);
                }
            } else {
                chunk().writeOp(OpCode::OP_POP);
            }

            consume(TokenType::RPAREN, "Expected ')' after for clauses");

            emitLoop(loop.startIP);
            loop.startIP = incrementStart;
            patchJump(bodyJump);
        } else {
            loop.continueIP = loop.startIP;
        }

        statement();
        emitLoop(loop.startIP);

        if (exitJump != -1) {
            patchJump(exitJump);
            chunk().writeOp(OpCode::OP_POP);
        }

        for (int breakJump : loop.breakJumps) {
            patchJump(breakJump);
        }

        endScope();
    } else if (current.type == TokenType::LBRACE) {
        blockStatement();
    } else {
        // Expression statement or assignment
        expression();
        if (current.type == TokenType::EQUAL || current.type == TokenType::PLUS_EQUAL ||
            current.type == TokenType::MINUS_EQUAL || current.type == TokenType::STAR_EQUAL ||
            current.type == TokenType::SLASH_EQUAL || current.type == TokenType::PERCENT_EQUAL) {
            TokenType assignOp = current.type;
            advance();

            if (!chunk().code.empty() && static_cast<OpCode>(chunk().code.back()) == OpCode::OP_GET_INDEX) {
                chunk().code.pop_back(); // remove OP_GET_INDEX
                if (assignOp != TokenType::EQUAL) {
                    chunk().writeOp(OpCode::OP_DUP_2);
                    chunk().writeOp(OpCode::OP_GET_INDEX);
                    expression();
                    if (assignOp == TokenType::PLUS_EQUAL) chunk().writeOp(OpCode::OP_ADD);
                    else if (assignOp == TokenType::MINUS_EQUAL) chunk().writeOp(OpCode::OP_SUBTRACT);
                    else if (assignOp == TokenType::STAR_EQUAL) chunk().writeOp(OpCode::OP_MULTIPLY);
                    else if (assignOp == TokenType::SLASH_EQUAL) chunk().writeOp(OpCode::OP_DIVIDE);
                    else if (assignOp == TokenType::PERCENT_EQUAL) chunk().writeOp(OpCode::OP_MODULO);
                } else {
                    expression();
                }
                consume(TokenType::TILDE, "Every statement must end with '~'");
                chunk().writeOp(OpCode::OP_SET_INDEX);
                chunk().writeOp(OpCode::OP_POP);
            } else if (chunk().code.size() >= 3 && static_cast<OpCode>(chunk().code[chunk().code.size() - 3]) == OpCode::OP_GET_MEMBER) {
                uint16_t memberIdx = (static_cast<uint16_t>(chunk().code[chunk().code.size() - 2]) << 8) | chunk().code.back();
                chunk().code.pop_back(); chunk().code.pop_back(); chunk().code.pop_back();
                if (assignOp != TokenType::EQUAL) {
                    chunk().writeOp(OpCode::OP_DUP);
                    chunk().writeOp(OpCode::OP_GET_MEMBER);
                    chunk().write16(memberIdx);
                    expression();
                    if (assignOp == TokenType::PLUS_EQUAL) chunk().writeOp(OpCode::OP_ADD);
                    else if (assignOp == TokenType::MINUS_EQUAL) chunk().writeOp(OpCode::OP_SUBTRACT);
                    else if (assignOp == TokenType::STAR_EQUAL) chunk().writeOp(OpCode::OP_MULTIPLY);
                    else if (assignOp == TokenType::SLASH_EQUAL) chunk().writeOp(OpCode::OP_DIVIDE);
                    else if (assignOp == TokenType::PERCENT_EQUAL) chunk().writeOp(OpCode::OP_MODULO);
                } else {
                    expression();
                }
                consume(TokenType::TILDE, "Every statement must end with '~'");
                chunk().writeOp(OpCode::OP_SET_MEMBER);
                chunk().write16(memberIdx);
                chunk().writeOp(OpCode::OP_POP);
            } else if (chunk().code.size() >= 3 && static_cast<OpCode>(chunk().code[chunk().code.size() - 3]) == OpCode::OP_GET_LOCAL) {
                uint16_t localSlot = (static_cast<uint16_t>(chunk().code[chunk().code.size() - 2]) << 8) | chunk().code.back();
                chunk().code.pop_back(); chunk().code.pop_back(); chunk().code.pop_back();

                if (localSlot < currentContext->locals.size() && currentContext->locals[localSlot].isConst) {
                    error("Cannot reassign constant variable '" + currentContext->locals[localSlot].name + "'.", "Compiler Error");
                }

                if (assignOp != TokenType::EQUAL) {
                    chunk().writeOp(OpCode::OP_GET_LOCAL);
                    chunk().write16(localSlot);
                    expression();
                    if (assignOp == TokenType::PLUS_EQUAL) chunk().writeOp(OpCode::OP_ADD);
                    else if (assignOp == TokenType::MINUS_EQUAL) chunk().writeOp(OpCode::OP_SUBTRACT);
                    else if (assignOp == TokenType::STAR_EQUAL) chunk().writeOp(OpCode::OP_MULTIPLY);
                    else if (assignOp == TokenType::SLASH_EQUAL) chunk().writeOp(OpCode::OP_DIVIDE);
                    else if (assignOp == TokenType::PERCENT_EQUAL) chunk().writeOp(OpCode::OP_MODULO);
                } else {
                    expression();
                }
                consume(TokenType::TILDE, "Every statement must end with '~'");
                chunk().writeOp(OpCode::OP_SET_LOCAL);
                chunk().write16(localSlot);
                chunk().writeOp(OpCode::OP_POP);
            } else if (chunk().code.size() >= 3 && static_cast<OpCode>(chunk().code[chunk().code.size() - 3]) == OpCode::OP_GET_UPVALUE) {
                uint16_t upvalueSlot = (static_cast<uint16_t>(chunk().code[chunk().code.size() - 2]) << 8) | chunk().code.back();
                chunk().code.pop_back(); chunk().code.pop_back(); chunk().code.pop_back();

                if (upvalueSlot < currentContext->upvalues.size() && currentContext->upvalues[upvalueSlot].isConst) {
                    error("Cannot reassign constant variable '" + currentContext->upvalues[upvalueSlot].name + "'.", "Compiler Error");
                }

                if (assignOp != TokenType::EQUAL) {
                    chunk().writeOp(OpCode::OP_GET_UPVALUE);
                    chunk().write16(upvalueSlot);
                    expression();
                    if (assignOp == TokenType::PLUS_EQUAL) chunk().writeOp(OpCode::OP_ADD);
                    else if (assignOp == TokenType::MINUS_EQUAL) chunk().writeOp(OpCode::OP_SUBTRACT);
                    else if (assignOp == TokenType::STAR_EQUAL) chunk().writeOp(OpCode::OP_MULTIPLY);
                    else if (assignOp == TokenType::SLASH_EQUAL) chunk().writeOp(OpCode::OP_DIVIDE);
                    else if (assignOp == TokenType::PERCENT_EQUAL) chunk().writeOp(OpCode::OP_MODULO);
                } else {
                    expression();
                }
                consume(TokenType::TILDE, "Every statement must end with '~'");
                chunk().writeOp(OpCode::OP_SET_UPVALUE);
                chunk().write16(upvalueSlot);
                chunk().writeOp(OpCode::OP_POP);
            } else if (chunk().code.size() >= 3 && static_cast<OpCode>(chunk().code[chunk().code.size() - 3]) == OpCode::OP_GET_GLOBAL) {
                uint16_t nameIdx = (static_cast<uint16_t>(chunk().code[chunk().code.size() - 2]) << 8) | chunk().code.back();
                chunk().code.pop_back(); chunk().code.pop_back(); chunk().code.pop_back();

                std::string varName = chunk().constants[nameIdx].str;
                if (globalConsts.find(varName) != globalConsts.end() && globalConsts[varName]) {
                    error("Cannot reassign constant variable '" + varName + "'.", "Compiler Error");
                }

                if (assignOp != TokenType::EQUAL) {
                    chunk().writeOp(OpCode::OP_GET_GLOBAL);
                    chunk().write16(nameIdx);
                    expression();
                    if (assignOp == TokenType::PLUS_EQUAL) chunk().writeOp(OpCode::OP_ADD);
                    else if (assignOp == TokenType::MINUS_EQUAL) chunk().writeOp(OpCode::OP_SUBTRACT);
                    else if (assignOp == TokenType::STAR_EQUAL) chunk().writeOp(OpCode::OP_MULTIPLY);
                    else if (assignOp == TokenType::SLASH_EQUAL) chunk().writeOp(OpCode::OP_DIVIDE);
                    else if (assignOp == TokenType::PERCENT_EQUAL) chunk().writeOp(OpCode::OP_MODULO);
                } else {
                    expression();
                }
                consume(TokenType::TILDE, "Every statement must end with '~'");
                chunk().writeOp(OpCode::OP_SET_GLOBAL);
                chunk().write16(nameIdx);
                chunk().writeOp(OpCode::OP_POP);
            } else {
                errorAt(prev, "Invalid assignment target.", "Syntax Error");
            }
        } else {
            consume(TokenType::TILDE, "Every statement must end with '~'");
            chunk().writeOp(OpCode::OP_POP);
        }
    }
}

bool Compiler::compile() {
    CompilerContext scriptContext(targetChunk);
    scriptContext.locals.push_back(Local{"", 0, false, TypeSpec{TypeKind::ANY}});
    currentContext = &scriptContext;

    while (current.type != TokenType::END_OF_FILE && !hasError) {
        statement();
    }
    chunk().writeOp(OpCode::OP_RETURN);

    return !hasError;
}
