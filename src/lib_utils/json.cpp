// Simplistic standalone JSON-parser

#include "json.hpp"
#include <stdexcept>

namespace json {
namespace {

struct Token {
	enum Type {
		EOF_ = 0,
		LBRACE,
		RBRACE,
		LBRACKET,
		RBRACKET,
		STRING,
		NUMBER,
		BOOLEAN,
		COLON,
		COMMA,
	};

	std::string lexem;
	Type type;
};

class Tokenizer {
	public:
		Tokenizer(const char* text_, size_t len) {
			text = text_;
			textEnd = text_ + len;
			decodeToken();
		}

		const Token& front() const {
			return curr;
		}
		bool empty() const {
			return curr.type == Token::EOF_;
		}
		void popFront() {
			decodeToken();
		}

	private:
		void decodeToken() {
			while(whitespace(frontChar()))
				++text;

			curr.lexem = "";
			switch(frontChar()) {
			case '\0':
				curr.type = Token::EOF_;
				break;
			case '[':
				accept();
				curr.type = Token::LBRACKET;
				break;
			case ']':
				accept();
				curr.type = Token::RBRACKET;
				break;
			case '{':
				accept();
				curr.type = Token::LBRACE;
				break;
			case '}':
				accept();
				curr.type = Token::RBRACE;
				break;
			case ':':
				accept();
				curr.type = Token::COLON;
				break;
			case ',':
				accept();
				curr.type = Token::COMMA;
				break;
			case '"':
				accept();

				while(frontChar() != '"' && frontChar() != '\0') {
					if(frontChar() == '\\') {
						accept();
						curr.lexem.pop_back();

						// escape sequence
						accept();
					} else {
						accept();
					}
				}

				accept();

				curr.type = Token::STRING;
				curr.lexem = curr.lexem.substr(1, curr.lexem.size() - 2);
				break;
			case 't':
				curr.type = Token::BOOLEAN;
				expect('t');
				expect('r');
				expect('u');
				expect('e');
				break;
			case 'f':
				curr.type = Token::BOOLEAN;
				expect('f');
				expect('a');
				expect('l');
				expect('s');
				expect('e');
				break;
			case '-': case '0': case '1': case '2':
			case '3': case '4': case '5': case '6':
			case '7': case '8': case '9': {
				curr.type = Token::NUMBER;

				if(frontChar() == '-')
					accept();

				while(isdigit(frontChar()))
					accept();

				break;
			}
			default: {
				std::string msg = "Unknown char '";
				msg += frontChar();
				msg += "'";
				throw std::runtime_error(msg);
			}
			}
		}

		void expect(char c) {
			if(frontChar() != c)
				throw std::runtime_error("Unexpected character");

			accept();
		}

		void accept() {
			curr.lexem += frontChar();
			++text;
		}

		char frontChar() const {
			if(text >= textEnd)
				return 0;

			return *text;
		}

		static bool whitespace(char c) {
			return c == ' ' || c == '\n' || c == '\r' || c == '\t';
		}

		const char* text;
		const char* textEnd;
		Token curr;
};

using namespace json;

std::string expect(Tokenizer& tk, Token::Type type) {
	auto front = tk.front();

	if(front.type != type) {
		std::string msg;

		if(front.type == Token::EOF_)
			msg += "Unexpected end of file found";
		else {
			msg += "Unexpected token '" + front.lexem + "'";
			msg += " of type " + std::to_string(front.type);
			msg += " instead of " + std::to_string(type);
		}

		throw std::runtime_error(msg);
	}

	auto r = front.lexem;
	tk.popFront();
	return r;
}

Value parseValue(Tokenizer& tk);

Value parseObject(Tokenizer& tk) {
	Value r;
	r.type = Value::Type::Object;
	expect(tk, Token::LBRACE);
	int idx = 0;

	while(tk.front().type != Token::RBRACE) {
		if(idx > 0)
			expect(tk, Token::COMMA);

		auto const name = expect(tk, Token::STRING);
		expect(tk, Token::COLON);
		r.objectValue[name] = parseValue(tk);
		++idx;
	}

	expect(tk, Token::RBRACE);
	return r;
}

Value parseArray(Tokenizer& tk) {
	Value r;
	r.type = Value::Type::Array;
	expect(tk, Token::LBRACKET);
	int idx = 0;

	while(tk.front().type != Token::RBRACKET) {
		if(idx > 0)
			expect(tk, Token::COMMA);

		r.arrayValue.push_back(parseValue(tk));
		++idx;
	}

	expect(tk, Token::RBRACKET);
	return r;
}

Value parseValue(Tokenizer& tk) {
	if(tk.front().type == Token::LBRACKET) {
		return parseArray(tk);
	} else if(tk.front().type == Token::LBRACE) {
		return parseObject(tk);
	} else if(tk.front().type == Token::BOOLEAN) {
		Value r;
		r.type = Value::Type::Boolean;
		r.boolValue = expect(tk, Token::BOOLEAN) == "true";
		return r;
	} else if(tk.front().type == Token::NUMBER) {
		Value r;
		r.type = Value::Type::Integer;
		r.intValue = atoi(expect(tk, Token::NUMBER).c_str());
		return r;
	} else {
		Value r;
		r.type = Value::Type::String;
		r.stringValue = expect(tk, Token::STRING);
		return r;
	}
}
} /*anonymous*/

void Value::enforceType(Type expected) const {
	if(type != expected)
		throw std::runtime_error("Type error");
}

Value const& Value::operator[] (const char* name) const {
	enforceType(Type::Object);
	auto it = objectValue.find(name);
	if(it == objectValue.end())
		throw std::runtime_error("Member '" + std::string(name) + "' was not found");

	return (*it).value;
}

Value parse(const std::string &s) {
	Tokenizer tokenizer(s.c_str(), s.size());
	return parseObject(tokenizer);
}
}
