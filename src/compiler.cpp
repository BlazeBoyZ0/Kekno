#include "compiler.h"
#include <iostream>

Compiler::Compiler(const std::string& src, Chunk& targetChunk)
    : lexer(src), chunk(targetChunk) {
    advance();
}

void Compiler::advance() {
    current = lexer.nextToken();
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
        std::cout << "[Syntax Error]: " << errMsg << " (Found: '" << current.text << "')" << std::endl;
        hasError = true;
    }
}

void Compiler::emitConstant(Value value) {
    chunk.writeOp(OpCode::OP_CONSTANT);
    chunk.writeByte(chunk.addConstant(value));
}

int Compiler::emitJump(OpCode op) {
    chunk.writeOp(op);
    chunk.writeByte(0xff);
    chunk.writeByte(0xff);
    return static_cast<int>(chunk.code.size() - 2);
}

void Compiler::patchJump(int offset) {
    int jump = static_cast<int>(chunk.code.size() - offset - 2);
    if (jump > 0xffff) {
        std::cout << "[Compiler Error]: Too much code to jump over." << std::endl;
        hasError = true;
        return;
    }
    chunk.code[offset] = static_cast<uint8_t>((jump >> 8) & 0xff);
    chunk.code[offset + 1] = static_cast<uint8_t>(jump & 0xff);
}

void Compiler::emitLoop(int loopStart) {
    chunk.writeOp(OpCode::OP_LOOP);
    int jump = static_cast<int>(chunk.code.size() - loopStart + 2);
    if (jump > 0xffff) {
        std::cout << "[Compiler Error]: Loop body too large." << std::endl;
        hasError = true;
        return;
    }
    chunk.writeByte(static_cast<uint8_t>((jump >> 8) & 0xff));
    chunk.writeByte(static_cast<uint8_t>(jump & 0xff));
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
        chunk.writeOp(OpCode::OP_TRUE);
        advance();
    } else if (current.type == TokenType::FALSE) {
        chunk.writeOp(OpCode::OP_FALSE);
        advance();
    } else if (current.type == TokenType::NIL) {
        chunk.writeOp(OpCode::OP_NIL);
        advance();
    } else if (current.type == TokenType::IDENTIFIER) {
        uint8_t nameIdx = chunk.addConstant(Value(current.text));
        chunk.writeOp(OpCode::OP_GET_GLOBAL);
        chunk.writeByte(nameIdx);
        advance();
    } else if (current.type == TokenType::LPAREN) {
        advance();
        expression();
        consume(TokenType::RPAREN, "Expected ')' after expression");
    } else {
        std::cout << "[Syntax Error]: Expected expression" << std::endl;
        hasError = true;
    }
}

void Compiler::unary() {
    if (hasError) return;
    if (current.type == TokenType::BANG || current.type == TokenType::NOT) {
        advance();
        unary();
        chunk.writeOp(OpCode::OP_NOT);
    } else if (current.type == TokenType::MINUS) {
        advance();
        unary();
        emitConstant(Value(-1.0));
        chunk.writeOp(OpCode::OP_MULTIPLY);
    } else {
        primary();
    }
}

void Compiler::power() {
    if (hasError) return;
    unary();
    if (current.type == TokenType::CARET) {
        advance();
        power();
        chunk.writeOp(OpCode::OP_POWER);
    }
}

void Compiler::factor() {
    if (hasError) return;
    power();
    while (current.type == TokenType::STAR || current.type == TokenType::SLASH || current.type == TokenType::PERCENT) {
        TokenType op = current.type;
        advance();
        power();
        if (op == TokenType::STAR) chunk.writeOp(OpCode::OP_MULTIPLY);
        else if (op == TokenType::SLASH) chunk.writeOp(OpCode::OP_DIVIDE);
        else chunk.writeOp(OpCode::OP_MODULO);
    }
}

void Compiler::term() {
    if (hasError) return;
    factor();
    while (current.type == TokenType::PLUS || current.type == TokenType::MINUS) {
        TokenType op = current.type;
        advance();
        factor();
        if (op == TokenType::PLUS) chunk.writeOp(OpCode::OP_ADD);
        else chunk.writeOp(OpCode::OP_SUBTRACT);
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
            chunk.writeOp(OpCode::OP_GREATER);
        } else if (op == TokenType::GREATER_EQUAL) {
            chunk.writeOp(OpCode::OP_LESS);
            chunk.writeOp(OpCode::OP_NOT);
        } else if (op == TokenType::LESS) {
            chunk.writeOp(OpCode::OP_LESS);
        } else if (op == TokenType::LESS_EQUAL) {
            chunk.writeOp(OpCode::OP_GREATER);
            chunk.writeOp(OpCode::OP_NOT);
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
            chunk.writeOp(OpCode::OP_EQUAL);
        } else {
            chunk.writeOp(OpCode::OP_EQUAL);
            chunk.writeOp(OpCode::OP_NOT);
        }
    }
}

void Compiler::andExpression() {
    if (hasError) return;
    equality();
    while (current.type == TokenType::AND) {
        advance();
        int endJump = emitJump(OpCode::OP_JUMP_IF_FALSE);
        chunk.writeOp(OpCode::OP_POP);
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
        chunk.writeOp(OpCode::OP_POP);
        andExpression();
        patchJump(endJump);
    }
}

void Compiler::expression() {
    if (hasError) return;
    orExpression();
}

void Compiler::varDeclaration() {
    advance();
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
    uint8_t nameIdx = chunk.addConstant(Value(varName));
    chunk.writeOp(OpCode::OP_DEFINE_GLOBAL);
    chunk.writeByte(nameIdx);
}

void Compiler::blockStatement() {
    advance(); // consume '{'
    while (current.type != TokenType::RBRACE && current.type != TokenType::END_OF_FILE && !hasError) {
        statement();
    }
    consume(TokenType::RBRACE, "Expected '}' after block");
}

void Compiler::ifStatement() {
    advance(); // consume 'if'
    consume(TokenType::LPAREN, "Expected '(' after 'if'");
    expression();
    consume(TokenType::RPAREN, "Expected ')' after condition");

    int thenJump = emitJump(OpCode::OP_JUMP_IF_FALSE);
    chunk.writeOp(OpCode::OP_POP);

    statement();

    int elseJump = emitJump(OpCode::OP_JUMP);
    patchJump(thenJump);
    chunk.writeOp(OpCode::OP_POP);

    if (match(TokenType::ELSE)) {
        statement();
    }
    patchJump(elseJump);
}

void Compiler::whileStatement() {
    advance(); // consume 'while'
    int loopStart = static_cast<int>(chunk.code.size());

    consume(TokenType::LPAREN, "Expected '(' after 'while'");
    expression();
    consume(TokenType::RPAREN, "Expected ')' after condition");

    int exitJump = emitJump(OpCode::OP_JUMP_IF_FALSE);
    chunk.writeOp(OpCode::OP_POP);

    statement();

    emitLoop(loopStart);

    patchJump(exitJump);
    chunk.writeOp(OpCode::OP_POP);
}

void Compiler::statement() {
    if (hasError) return;
    if (current.type == TokenType::LET) {
        varDeclaration();
    } else if (current.type == TokenType::PRINT) {
        advance();
        expression();
        consume(TokenType::TILDE, "Every statement must end with '~'");
        chunk.writeOp(OpCode::OP_PRINT);
    } else if (current.type == TokenType::IF) {
        ifStatement();
    } else if (current.type == TokenType::WHILE) {
        whileStatement();
    } else if (current.type == TokenType::LBRACE) {
        blockStatement();
    } else if (current.type == TokenType::IDENTIFIER) {
        Token identToken = current;
        advance();
        if (current.type == TokenType::EQUAL) {
            std::string varName = identToken.text;
            advance();
            expression();
            consume(TokenType::TILDE, "Every statement must end with '~'");
            uint8_t nameIdx = chunk.addConstant(Value(varName));
            chunk.writeOp(OpCode::OP_SET_GLOBAL);
            chunk.writeByte(nameIdx);
        } else {
            uint8_t nameIdx = chunk.addConstant(Value(identToken.text));
            chunk.writeOp(OpCode::OP_GET_GLOBAL);
            chunk.writeByte(nameIdx);

            while (current.type == TokenType::STAR || current.type == TokenType::SLASH || current.type == TokenType::PERCENT) {
                TokenType op = current.type;
                advance();
                power();
                if (op == TokenType::STAR) chunk.writeOp(OpCode::OP_MULTIPLY);
                else if (op == TokenType::SLASH) chunk.writeOp(OpCode::OP_DIVIDE);
                else chunk.writeOp(OpCode::OP_MODULO);
            }
            while (current.type == TokenType::PLUS || current.type == TokenType::MINUS) {
                TokenType op = current.type;
                advance();
                factor();
                if (op == TokenType::PLUS) chunk.writeOp(OpCode::OP_ADD);
                else chunk.writeOp(OpCode::OP_SUBTRACT);
            }
            while (current.type == TokenType::GREATER || current.type == TokenType::GREATER_EQUAL ||
                   current.type == TokenType::LESS || current.type == TokenType::LESS_EQUAL) {
                TokenType op = current.type;
                advance();
                term();
                if (op == TokenType::GREATER) chunk.writeOp(OpCode::OP_GREATER);
                else if (op == TokenType::GREATER_EQUAL) { chunk.writeOp(OpCode::OP_LESS); chunk.writeOp(OpCode::OP_NOT); }
                else if (op == TokenType::LESS) chunk.writeOp(OpCode::OP_LESS);
                else if (op == TokenType::LESS_EQUAL) { chunk.writeOp(OpCode::OP_GREATER); chunk.writeOp(OpCode::OP_NOT); }
            }
            while (current.type == TokenType::EQUAL_EQUAL || current.type == TokenType::BANG_EQUAL) {
                TokenType op = current.type;
                advance();
                relational();
                if (op == TokenType::EQUAL_EQUAL) chunk.writeOp(OpCode::OP_EQUAL);
                else { chunk.writeOp(OpCode::OP_EQUAL); chunk.writeOp(OpCode::OP_NOT); }
            }
            while (current.type == TokenType::AND) {
                advance();
                int endJump = emitJump(OpCode::OP_JUMP_IF_FALSE);
                chunk.writeOp(OpCode::OP_POP);
                equality();
                patchJump(endJump);
            }
            while (current.type == TokenType::OR) {
                advance();
                int elseJump = emitJump(OpCode::OP_JUMP_IF_FALSE);
                int endJump = emitJump(OpCode::OP_JUMP);
                patchJump(elseJump);
                chunk.writeOp(OpCode::OP_POP);
                andExpression();
                patchJump(endJump);
            }

            consume(TokenType::TILDE, "Every statement must end with '~'");
            chunk.writeOp(OpCode::OP_POP);
        }
    } else {
        expression();
        consume(TokenType::TILDE, "Every statement must end with '~'");
        chunk.writeOp(OpCode::OP_POP);
    }
}

bool Compiler::compile() {
    while (current.type != TokenType::END_OF_FILE && !hasError) {
        statement();
    }
    chunk.writeOp(OpCode::OP_RETURN);
    return !hasError;
}
