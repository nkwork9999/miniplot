#pragma once
#include <string>
#include <sstream>
#include <vector>
#include <iomanip>
#include <limits>
#include <locale>
namespace miniplot_html {
using std::string;
// Helper: Escape strings for JavaScript
inline string EscapeString(const string &input) {
	string output;
	output.reserve(input.size());
	for (char c : input) {
		switch (c) {
		case '\'':
			output += "\\'";
			break;
		case '"':
			output += "\\\"";
			break;
		case '\\':
			output += "\\\\";
			break;
		case '\n':
			output += "\\n";
			break;
		case '\r':
			output += "\\r";
			break;
		case '\t':
			output += "\\t";
			break;
		case '<':
			output += "\\x3C";
			break;
		case '>':
			output += "\\x3E";
			break;
		default:
			if (static_cast<unsigned char>(c) < 32) {
				const char *hex = "0123456789abcdef";
				auto byte = static_cast<unsigned char>(c);
				output += "\\x";
				output += hex[byte >> 4];
				output += hex[byte & 15];
			} else {
				output += c;
			}
		}
	}
	return output;
}

// HTML text and JavaScript strings have different escaping rules.
inline string EscapeHTML(const string &input) {
	string out;
	for (char c : input) {
		switch (c) {
		case '&':
			out += "&amp;";
			break;
		case '<':
			out += "&lt;";
			break;
		case '>':
			out += "&gt;";
			break;
		case '"':
			out += "&quot;";
			break;
		case '\'':
			out += "&#39;";
			break;
		default:
			out += c;
		}
	}
	return out;
}

inline string NumberLabel(double value) {
	std::ostringstream stream;
	stream.imbue(std::locale::classic());
	stream << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
	return stream.str();
}

inline string QuoteJSON(const string &input) {
	string output = "\"";
	for (unsigned char c : input) {
		if (c == '"' || c == '\\') {
			output += '\\';
			output += static_cast<char>(c);
		} else if (c < 32) {
			const char *hex = "0123456789abcdef";
			output += "\\u00";
			output += hex[c >> 4];
			output += hex[c & 15];
		} else {
			output += static_cast<char>(c);
		}
	}
	return output + '"';
}

// JSON stays data: parse an escaped string instead of inserting a JavaScript
// object literal (which would also give __proto__ different semantics).
inline string GeneratePlotlyHTML(const string &data_json, const string &layout_json, const string &config_json,
                                 const string &title = "Miniplot") {
	std::ostringstream html;
	html << "<!DOCTYPE html>\n<html><head><meta charset=\"UTF-8\"><title>" << EscapeHTML(title)
	     << "</title><style>body{margin:0;font-family:system-ui,sans-serif}#chart{width:100%;min-height:600px}</style>"
	     << "</head><body><div id=\"chart\"></div>\n"
	     << "<script src=\"https://cdn.plot.ly/plotly-2.27.0.min.js\"></script>\n<script>\n"
	     << "var data = JSON.parse('" << EscapeString(data_json) << "');\n"
	     << "var layout = JSON.parse('" << EscapeString(layout_json) << "');\n"
	     << "var config = JSON.parse('" << EscapeString(config_json) << "');\n"
	     << "Plotly.newPlot('chart', data, layout, config);\n</script></body></html>";
	return html.str();
}

// Generate HTML
inline string GenerateHTML(const std::vector<string> &x_data, const std::vector<double> &y_data, const string &title,
                           const string &chart_type) {
	std::ostringstream x_json;
	x_json.imbue(std::locale::classic());
	x_json << "[";
	for (size_t i = 0; i < x_data.size(); i++) {
		if (i > 0)
			x_json << ", ";
		x_json << "'" << EscapeString(x_data[i]) << "'";
	}
	x_json << "]";

	std::ostringstream y_json;
	y_json.imbue(std::locale::classic());
	y_json << std::setprecision(std::numeric_limits<double>::max_digits10);
	y_json << "[";
	for (size_t i = 0; i < y_data.size(); i++) {
		if (i > 0)
			y_json << ", ";
		y_json << y_data[i];
	}
	y_json << "]";

	string plotly_type = "bar";
	string mode = "";
	string fill = "";

	if (chart_type == "line") {
		plotly_type = "scatter";
		mode = "mode: 'lines+markers',";
	} else if (chart_type == "scatter") {
		plotly_type = "scatter";
		mode = "mode: 'markers',";
	} else if (chart_type == "area") {
		plotly_type = "scatter";
		mode = "mode: 'lines',";
		fill = "fill: 'tozeroy',";
	}

	std::ostringstream html;
	html << R"(<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>)"
	     << EscapeHTML(title) << R"(</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Arial, sans-serif; background: #f5f5f5; padding: 20px; }
        .container { max-width: 1400px; margin: 0 auto; background: #fff; padding: 30px; border-radius: 8px; box-shadow: 0 1px 3px rgba(0,0,0,0.1); }
        h1 { margin: 0 0 20px; color: #333; font-size: 28px; text-align: center; }
        #chart { width: 100%; height: 600px; }
    </style>
</head>
<body>
    <div class="container">
        <h1>)"
	     << EscapeHTML(title) << R"(</h1>
        <div id="chart"></div>
    </div>
    <script src="https://cdn.plot.ly/plotly-2.27.0.min.js"></script>
    <script>
    var data = [{
        x: )"
	     << x_json.str() << R"(,
        y: )"
	     << y_json.str() << R"(,
        type: ')"
	     << plotly_type << R"(',
        )"
	     << mode << fill << R"(
        marker: { color: 'rgb(59, 130, 246)', size: 10 },
        line: { color: 'rgb(59, 130, 246)', width: 3 }
    }];
    var layout = {
        xaxis: { showgrid: true, gridcolor: '#e5e5e5' },
        yaxis: { title: 'Value', showgrid: true, gridcolor: '#e5e5e5' },
        plot_bgcolor: '#fff',
        paper_bgcolor: '#fff',
        margin: { t: 40, r: 40, b: 60, l: 70 },
        autosize: true
    };
    Plotly.newPlot('chart', data, layout, { responsive: true, displayModeBar: true });
    window.addEventListener('resize', function() { Plotly.Plots.resize('chart'); });
    </script>
</body>
</html>)";

	return html.str();
}

// Generate 3D Scatter HTML (without timestamp)
inline string Generate3DScatterHTML(const std::vector<double> &x_data, const std::vector<double> &y_data,
                                    const std::vector<double> &z_data, const string &title) {
	std::ostringstream x_json, y_json, z_json;
	for (auto *stream : {&x_json, &y_json, &z_json}) {
		stream->imbue(std::locale::classic());
		*stream << std::setprecision(std::numeric_limits<double>::max_digits10);
	}

	x_json << "[";
	for (size_t i = 0; i < x_data.size(); i++) {
		if (i > 0)
			x_json << ", ";
		x_json << x_data[i];
	}
	x_json << "]";

	y_json << "[";
	for (size_t i = 0; i < y_data.size(); i++) {
		if (i > 0)
			y_json << ", ";
		y_json << y_data[i];
	}
	y_json << "]";

	z_json << "[";
	for (size_t i = 0; i < z_data.size(); i++) {
		if (i > 0)
			z_json << ", ";
		z_json << z_data[i];
	}
	z_json << "]";

	std::ostringstream html;
	html << R"(<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>)"
	     << EscapeHTML(title) << R"(</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Arial, sans-serif; background: #f5f5f5; padding: 20px; }
        .container { max-width: 1400px; margin: 0 auto; background: #fff; padding: 30px; border-radius: 8px; box-shadow: 0 1px 3px rgba(0,0,0,0.1); }
        h1 { margin: 0 0 20px; color: #333; font-size: 28px; text-align: center; }
        #chart { width: 100%; height: 700px; }
    </style>
</head>
<body>
    <div class="container">
        <h1>)"
	     << EscapeHTML(title) << R"(</h1>
        <div id="chart"></div>
    </div>
    <script src="https://cdn.plot.ly/plotly-2.27.0.min.js"></script>
    <script>
    var data = [{
        x: )"
	     << x_json.str() << R"(,
        y: )"
	     << y_json.str() << R"(,
        z: )"
	     << z_json.str() << R"(,
        mode: 'markers',
        type: 'scatter3d',
        marker: {
            size: 8,
            color: )"
	     << z_json.str() << R"(,
            colorscale: 'Viridis',
            showscale: true,
            colorbar: {
                title: 'Z Value',
                thickness: 20,
                len: 0.7
            }
        }
    }];
    var layout = {
        scene: {
            xaxis: { title: 'X Axis', showgrid: true, gridcolor: '#e5e5e5' },
            yaxis: { title: 'Y Axis', showgrid: true, gridcolor: '#e5e5e5' },
            zaxis: { title: 'Z Axis', showgrid: true, gridcolor: '#e5e5e5' },
            bgcolor: '#fff'
        },
        paper_bgcolor: '#fff',
        margin: { t: 40, r: 40, b: 40, l: 40 },
        autosize: true
    };
    Plotly.newPlot('chart', data, layout, { responsive: true, displayModeBar: true });
    window.addEventListener('resize', function() { Plotly.Plots.resize('chart'); });
    </script>
</body>
</html>)";

	return html.str();
}

// Generate 3D Scatter HTML with timestamps
inline string Generate3DScatterHTMLWithTimestamp(const std::vector<double> &x_data, const std::vector<double> &y_data,
                                                 const std::vector<double> &z_data,
                                                 const std::vector<string> &timestamps, const string &title) {
	std::ostringstream x_json, y_json, z_json, timestamp_json;
	for (auto *stream : {&x_json, &y_json, &z_json}) {
		stream->imbue(std::locale::classic());
		*stream << std::setprecision(std::numeric_limits<double>::max_digits10);
	}

	x_json << "[";
	for (size_t i = 0; i < x_data.size(); i++) {
		if (i > 0)
			x_json << ", ";
		x_json << x_data[i];
	}
	x_json << "]";

	y_json << "[";
	for (size_t i = 0; i < y_data.size(); i++) {
		if (i > 0)
			y_json << ", ";
		y_json << y_data[i];
	}
	y_json << "]";

	z_json << "[";
	for (size_t i = 0; i < z_data.size(); i++) {
		if (i > 0)
			z_json << ", ";
		z_json << z_data[i];
	}
	z_json << "]";

	timestamp_json << "[";
	for (size_t i = 0; i < timestamps.size(); i++) {
		if (i > 0)
			timestamp_json << ", ";
		timestamp_json << "'" << EscapeString(timestamps[i]) << "'";
	}
	timestamp_json << "]";

	std::ostringstream html;
	html << R"(<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>)"
	     << EscapeHTML(title) << R"(</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Arial, sans-serif; background: #f5f5f5; padding: 20px; }
        .container { max-width: 1400px; margin: 0 auto; background: #fff; padding: 30px; border-radius: 8px; box-shadow: 0 1px 3px rgba(0,0,0,0.1); }
        h1 { margin: 0 0 20px; color: #333; font-size: 28px; text-align: center; }
        #chart { width: 100%; height: 700px; }
    </style>
</head>
<body>
    <div class="container">
        <h1>)"
	     << EscapeHTML(title) << R"(</h1>
        <div id="chart"></div>
    </div>
    <script src="https://cdn.plot.ly/plotly-2.27.0.min.js"></script>
    <script>
    var data = [{
        x: )"
	     << x_json.str() << R"(,
        y: )"
	     << y_json.str() << R"(,
        z: )"
	     << z_json.str() << R"(,
        text: )"
	     << timestamp_json.str() << R"(,
        mode: 'markers',
        type: 'scatter3d',
        marker: {
            size: 8,
            color: )"
	     << z_json.str() << R"(,
            colorscale: 'Viridis',
            showscale: true,
            colorbar: {
                title: 'Z Value',
                thickness: 20,
                len: 0.7
            }
        },
        hovertemplate: '<b>Timestamp:</b> %{text}<br>' +
                       '<b>X:</b> %{x:.2f}<br>' +
                       '<b>Y:</b> %{y:.2f}<br>' +
                       '<b>Z:</b> %{z:.2f}<br>' +
                       '<extra></extra>'
    }];
    var layout = {
        scene: {
            xaxis: { title: 'X Axis', showgrid: true, gridcolor: '#e5e5e5' },
            yaxis: { title: 'Y Axis', showgrid: true, gridcolor: '#e5e5e5' },
            zaxis: { title: 'Z Axis', showgrid: true, gridcolor: '#e5e5e5' },
            bgcolor: '#fff'
        },
        paper_bgcolor: '#fff',
        margin: { t: 40, r: 40, b: 40, l: 40 },
        autosize: true
    };
    Plotly.newPlot('chart', data, layout, { responsive: true, displayModeBar: true });
    window.addEventListener('resize', function() { Plotly.Plots.resize('chart'); });
    </script>
</body>
</html>)";

	return html.str();
}

} // namespace miniplot_html
