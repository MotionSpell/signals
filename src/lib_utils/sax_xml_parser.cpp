#include "sax_xml_parser.hpp"
#include <stdexcept>
#include <cassert>

void saxParse(span<const char> input, std::function<NodeStartFunc> onNodeStart, std::function<NodeEndFunc> onNodeEnd) {
	using namespace std;

	std::string content;
	bool voidContent = false;

	auto front = [&]() -> int {
		if(input.len == 0)
			throw runtime_error("Unexpected end of file");

		return input[0];
	};

	auto accept = [&](char c) {
		if(c != front())
			return false;

		input += 1;
		return true;
	};

	auto skipSpaces = [&]() {
		while(input.len && isspace(front()))
			input += 1;
	};

	auto parseString = [&]() {
		string r;
		accept('"');
		while(front() != '"') {
			r += front();
			input += 1;
		}
		accept('"');
		return r;
	};

	auto parseIdentifier = [&]() {
		string r;
		skipSpaces();
		while(input.len &&
		    (isalnum(front()) || front() == ':' || front() == '_' || front() == '-')) {
			r += front();
			input += 1;
		}
		return r;
	};

	auto parseNextTag = [&]() {
		while(input.len && front() != '<' && front() != '>' && front() != '/')
			input += 1;

		if(input.len == 0)
			return;

		if(accept('<')) {
			if(accept('?')) {
				// XML stuff
			} else if(accept('!')) {
				// comment
			} else if(accept('/')) {
				// closing tag
				auto id = parseIdentifier();
				onNodeEnd(id, content);
				content.clear();
				voidContent = true;
			} else {
				// opening tag
				content.clear();
				voidContent = false;
				auto id = parseIdentifier();
				skipSpaces();

				SmallMap<string, string> attr;

				while(front() != '>' && front() != '/' && front() != -1) {
					auto name = parseIdentifier();
					if(name.empty()) {
						string msg = "expected an XML attribute, got '";
						msg += front();
						msg += "'";
						throw runtime_error(msg);
					}
					skipSpaces();
					if(accept('='))
						attr[name] = parseString();
					skipSpaces();
				}

				onNodeStart(id, attr);
			}
		} else if(accept('>')) {
			// content
			while(input.len) {
				if (front() == '<') {
					auto inputClone = input;

					inputClone += 1;

					if (inputClone.len == 0)
						break;

					if (inputClone.ptr[0] == '/')
						break; // assume self closing tag

					if (content.empty())
						break; // assume child
				}

				if (front() == '&') {
					// escape?
					auto peek = [&](std::string str)->bool {
						if (input.len < str.size())
							return false;
						return str == std::string(input.ptr, str.size());
					};

					if (peek("&quot;")) {
						input += 6;
						content += '"';
						continue;
					} else if (peek("&apos;")) {
						input += 6;
						content += '\'';
						continue;
					} else if (peek("&lt;")) {
						input += 4;
						content += '<';
						continue;
					} else if (peek("&gt;")) {
						input += 4;
						content += '>';
						continue;
					} else if (peek("&amp;")) {
						input += 5;
						content += '&';
						continue;
					}
				}

				if (!voidContent)
					// strip empty first characters
					if (!content.empty() || !isspace(front()))
						content += front();

				input += 1;
			}
		} else if(accept('/')) {
			if(accept('>')) {
				// self closing tag
				auto id = parseIdentifier();
				assert(content == "");
				onNodeEnd(id, content);
			}
		} else
			assert(0); //char is dispatched but not processed: this is likely a programming error
	};

	while(input.len)
		parseNextTag();
}

