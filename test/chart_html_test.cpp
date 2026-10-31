#include "chart_html.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
struct CommaPunctuation : std::numpunct<char> {
	char do_decimal_point() const override {
		return ',';
	}
};
int main() {
	using namespace miniplot_html;
	assert(EscapeHTML("<a>&\"'") == "&lt;a&gt;&amp;&quot;&#39;");
	assert(EscapeString("</script>") == "\\x3C/script\\x3E");
	assert(EscapeString(std::string("\0\x01\x1f", 3)) == "\\x00\\x01\\x1f");
	assert(EscapeString("a\n\r\t") == "a\\n\\r\\t");
	assert(EscapeString(std::string(1, char(127))) == "\\x7f");
	// Raw NUL/control bytes must never reach HTML text nodes.
	assert(EscapeHTML(std::string("\0\x01\x7f", 3)) == "\xef\xbf\xbd\xef\xbf\xbd\xef\xbf\xbd");
	const std::string title = "日本語 <b>&'\"";
	std::locale::global(std::locale(std::locale::classic(), new CommaPunctuation));
	for (const auto &kind : {"bar", "line", "scatter", "area"}) {
		auto html = GenerateHTML({"one", "</script>"}, {0.1, 1.2345678901234567}, title, kind);
		assert(html.find("<title>" + EscapeHTML(title) + "</title>") != std::string::npos);
		assert(html.find("1.2345678901234567") != std::string::npos);
		assert(html.find("\\x3C/script\\x3E") != std::string::npos);
	}
	auto html3 = Generate3DScatterHTML({0.1}, {1.2345678901234567}, {2.5}, title);
	assert(html3.find("1.2345678901234567") != std::string::npos);
	auto htmlt = Generate3DScatterHTMLWithTimestamp({0.1}, {1.2345678901234567}, {2.5}, {"</script>"}, title);
	assert(htmlt.find("1.2345678901234567") != std::string::npos);
	assert(htmlt.find("\\x3C/script\\x3E") != std::string::npos);
	std::cout << "HTML encoding, precision, locale and chart variants passed\n";
}
