#include "compiler.h"
#include <iostream>

Compiler::Compiler(const std::string& src, Chunk& targetChunk)
    : lexer(src), chunk(targetChunk) {
    advance();
}

void Compiler::advance() {
    current = lexer.nextToken();
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

void Compiler::factor() {
    if (hasError) return;
    if (current.type == TokenType::NUMBER) {
        emitConstant(Value(current.numValue));
        advance();
    } else if (current.type == TokenType::STRING) {
        emitConstant(Value(current.strValue));
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

void Compiler::term() {
    if (hasError) return;
    factor();
    while (current.type == TokenType::STAR || current.type == TokenType::SLASH) {
        TokenType op = current.type;
        advance();
        factor();
        if (op == TokenType::STAR) chunk.writeOp(OpCode::OP_MULTIPLY);
        else chunk.writeOp(OpCode::OP_DIVIDE);
    }
}

void Compiler::expression() {
    if (hasError) return;
    term();
    while (current.type == TokenType::PLUS || current.type == TokenType::MINUS) {
        TokenType op = current.type;
        advance();
        term();
        if (op == TokenType::PLUS) chunk.writeOp(OpCode::OP_ADD);
        else chunk.writeOp(OpCode::OP_SUBTRACT);
    }
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

void Compiler::statement() {
    if (hasError) return;
    if (current.type == TokenType::LET) {
        varDeclaration();
    } else if (current.type == TokenType::PRINT) {
        advance();
        expression();
        consume(TokenType::TILDE, "Every statement must end with '~'");
        chunk.writeOp(OpCode::OP_PRINT);
    } else if (current.type == TokenType::IDENTIFIER) {
        Token next = lexer.nextToken();
        if (next.type == TokenType::EQUAL) {
            std::string varName = current.text;
            current = next;
            advance();
            expression();
            consume(TokenType::TILDE, "Every statement must end with '~'");
            uint8_t nameIdx = chunk.addConstant(Value(varName));
            chunk.writeOp(OpCode::OP_SET_GLOBAL);
            chunk.writeByte(nameIdx);
        } else {
            std::cout << "[Syntax Error]: Unexpected identifier sequence" << std::endl;
            hasError = true;
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
