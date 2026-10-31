// Included inside namespace duckdb after validation and file helpers.
static string ReportString(duckdb_yyjson::yyjson_val *obj, const char *key, bool required = false) {
	using namespace duckdb_yyjson;
	auto value = yyjson_obj_get(obj, key);
	if (!value && !required)
		return "";
	if (!value || !yyjson_is_str(value))
		throw InvalidInputException("Report %s must be a string", key);
	return string(yyjson_get_str(value), yyjson_get_len(value));
}
static string ReportJSON(duckdb_yyjson::yyjson_val *value) {
	using namespace duckdb_yyjson;
	std::unique_ptr<char, decltype(&free)> json(yyjson_val_write(value, 0, nullptr), free);
	if (!json)
		throw InvalidInputException("Unable to serialize report chart");
	return string(json.get());
}
static string GenerateReport(const string &spec) {
	using namespace duckdb_yyjson;
	std::unique_ptr<yyjson_doc, decltype(&yyjson_doc_free)> doc(yyjson_read(spec.data(), spec.size(), 0),
	                                                            yyjson_doc_free);
	if (!doc || !yyjson_is_obj(yyjson_doc_get_root(doc.get())))
		throw InvalidInputException("Report must be a JSON object");
	auto root = yyjson_doc_get_root(doc.get());
	auto title = ReportString(root, "title", true);
	auto sections = yyjson_obj_get(root, "sections");
	if (!sections || !yyjson_is_arr(sections) || !yyjson_arr_size(sections))
		throw InvalidInputException("Report sections must be a nonempty array");
	string body = "<h1>" + EscapeHTML(title) + "</h1>";
	string script = "window.miniplotReady = Promise.all([";
	idx_t chart_count = 0;
	size_t idx, max;
	yyjson_val *section;
	yyjson_arr_foreach(sections, idx, max, section) {
		if (!yyjson_is_obj(section))
			throw InvalidInputException("Each report section must be an object");
		auto heading = ReportString(section, "heading");
		auto text = ReportString(section, "text");
		auto data = yyjson_obj_get(section, "data");
		auto layout = yyjson_obj_get(section, "layout");
		auto config = yyjson_obj_get(section, "config");
		if (layout && !yyjson_is_obj(layout))
			throw InvalidInputException("Report layout must be a JSON object");
		if (config && !yyjson_is_obj(config))
			throw InvalidInputException("Report config must be a JSON object");
		if (heading.empty() && text.empty() && !data)
			throw InvalidInputException("Report section requires heading, text or data");
		body += "<section><h2>" + EscapeHTML(heading) + "</h2><p>" + EscapeHTML(text) + "</p>";
		if (data) {
			auto data_json = ReportJSON(data);
			auto layout_json = layout ? ReportJSON(layout) : "{}";
			auto config_json = config ? ReportJSON(config) : "{\"responsive\":true}";
			ValidatePlotlyJSON(data_json, "data", true);
			ValidatePlotlyJSON(layout_json, "layout", false);
			ValidatePlotlyJSON(config_json, "config", false);
			string id = "chart-" + std::to_string(chart_count);
			body += "<div class=\"chart\" id=\"" + id + "\"></div>";
			if (chart_count++)
				script += ',';
			script += "Plotly.newPlot('" + id + "',JSON.parse('" + EscapeString(data_json) + "'),JSON.parse('" +
			          EscapeString(layout_json) + "'),JSON.parse('" + EscapeString(config_json) + "'))";
		}
		body += "</section>";
	}
	script += "]);";
	// Keep an exact, non-executable specification for agents and reproducibility.
	string manifest;
	manifest.reserve(spec.size());
	for (char c : spec) {
		if (c == '<')
			manifest += "\\u003c";
		else if (c == '>')
			manifest += "\\u003e";
		else if (c == '&')
			manifest += "\\u0026";
		else if (c == 127)
			manifest += "\\u007f";
		else
			manifest += c;
	}
	body += "<script type=\"application/json\" id=\"miniplot-report\">" + manifest + "</script>";

	return "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><meta name=\"generator\" content=\"miniplot\"><meta "
	       "name=\"viewport\" content=\"width=device-width, initial-scale=1\"><title>" +
	       EscapeHTML(title) +
	       "</title><style>body{font:16px/1.6 system-ui,sans-serif;max-width:1100px;margin:0 "
	       "auto;padding:24px;color:#172033}h1{font-size:28px}h2{font-size:20px}p{white-space:pre-wrap;overflow-wrap:"
	       "anywhere}.chart{width:100%;height:480px}section{break-inside:avoid;margin-bottom:28px}@media "
	       "print{body{padding:0}.modebar{display:none!important}}</style></head><body>" +
	       body + "<script src=\"https://cdn.plot.ly/plotly-2.27.0.min.js\"></script><script>" + script +
	       "</script></body></html>";
}
template <bool RETURN_HTML>
static void ReportFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	result.SetVectorType(VectorType::FLAT_VECTOR);
	for (idx_t row = 0; row < args.size(); row++) {
		if (RowHasNull(args, row)) {
			result.SetValue(row, Value());
			continue;
		}
		auto html = GenerateReport(args.data[0].GetValue(row).ToString());
		if (RETURN_HTML)
			result.SetValue(row, Value(html));
		else
			SaveChartHTML(html, args.data[1].GetValue(row).ToString(), result, row);
	}
}
static void ExportFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	result.SetVectorType(VectorType::FLAT_VECTOR);
	for (idx_t row = 0; row < args.size(); row++) {
		if (RowHasNull(args, row)) {
			result.SetValue(row, Value());
			continue;
		}
		if (!Settings::Get<EnableExternalAccessSetting>(state.GetContext()))
			throw PermissionException("Chart export requires external access");
#if defined(__EMSCRIPTEN__)
		throw NotImplementedException("PNG/PDF export requires a native host with Node.js and Playwright");
#else
		auto output = args.data[1].GetValue(row).ToString();
		auto lower = ToLowerCase(output);
		if (output.find('\0') != string::npos || output.size() < 5 ||
		    (lower.substr(lower.size() - 4) != ".png" && lower.substr(lower.size() - 4) != ".pdf"))
			throw InvalidInputException("Export output must end in .png or .pdf and contain no NUL bytes");
		auto helper = std::getenv("MINIPLOT_EXPORTER");
		if (!helper || !*helper)
			throw InvalidInputException("Set MINIPLOT_EXPORTER to the absolute path of scripts/export-chart.cjs; "
			                            "install its optional Node dependencies");
		auto input = GenerateSafeFilename();
		struct Cleanup {
			string path;
			~Cleanup() {
				std::remove(path.c_str());
			}
		} cleanup {input};
		std::ofstream file(input);
		file << args.data[0].GetValue(row).ToString();
		file.close();
		if (!file)
			throw IOException("Failed to write export input");
		int status = 0;
#ifdef _WIN32
		const char *argv[] = {"node", helper, input.c_str(), output.c_str(), nullptr};
		status = static_cast<int>(_spawnvp(_P_WAIT, "node", argv));
#else
		char *argv[] = {const_cast<char *>("node"), const_cast<char *>(helper), const_cast<char *>(input.c_str()),
		                const_cast<char *>(output.c_str()), nullptr};
		pid_t pid;
		auto error = posix_spawnp(&pid, "node", nullptr, nullptr, argv, environ);
		if (error)
			throw IOException("Could not start Node.js exporter: %d", error);
		pid_t waited;
		do {
			waited = waitpid(pid, &status, 0);
		} while (waited < 0 && errno == EINTR);
		if (waited < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0)
			status = -1;
#endif
		if (status != 0)
			throw IOException("PNG/PDF export failed; inspect exporter error output");
		result.SetValue(row, Value(output));
#endif
	}
}
