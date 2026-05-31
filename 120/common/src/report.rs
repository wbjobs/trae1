use crate::flamegraph::HotSpotAnalysis;
use crate::models::{AggregatedResult, BenchmarkReport, Framework, RunStatus, Scenario};
use crate::profiler::analyze_hotspots;
use crate::stats::get_framework_crash_info;
use std::collections::HashMap;

pub fn generate_html_report(report: &BenchmarkReport, aggregated: &[AggregatedResult]) -> String {
    let framework_names: Vec<String> = report
        .config
        .targets
        .iter()
        .map(|f| f.name().to_string())
        .collect();

    let crash_info = get_framework_crash_info(&report.results);
    let crash_section = generate_crash_section_html(&crash_info);
    
    let hotspot_section = if report.config.profile.enabled {
        generate_hotspot_section_html(report)
    } else {
        String::new()
    };

    let mut charts_html = String::new();
    let mut tables_html = String::new();

    for scenario in &report.config.scenarios {
        let scenario_results: Vec<&AggregatedResult> = aggregated
            .iter()
            .filter(|r| r.scenario == *scenario)
            .collect();

        charts_html.push_str(&generate_scenario_charts(scenario, &scenario_results));
        tables_html.push_str(&generate_scenario_table(scenario, &scenario_results, &crash_info));
    }

    let timestamp = report.timestamp.format("%Y-%m-%d %H:%M:%S").to_string();

    format!(
        r#"<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Rust Web Framework Benchmark Report</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <style>
        * {{
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }}
        body {{
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            background: #f5f5f5;
            color: #333;
            padding: 20px;
        }}
        .container {{
            max-width: 1400px;
            margin: 0 auto;
        }}
        header {{
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            padding: 30px;
            border-radius: 10px;
            margin-bottom: 30px;
        }}
        h1 {{
            font-size: 2rem;
            margin-bottom: 10px;
        }}
        .metadata {{
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 15px;
            margin-top: 20px;
        }}
        .metadata-item {{
            background: rgba(255,255,255,0.2);
            padding: 10px 15px;
            border-radius: 5px;
        }}
        .metadata-label {{
            font-size: 0.85rem;
            opacity: 0.9;
        }}
        .metadata-value {{
            font-size: 1.1rem;
            font-weight: bold;
        }}
        .section {{
            background: white;
            border-radius: 10px;
            padding: 25px;
            margin-bottom: 30px;
            box-shadow: 0 2px 10px rgba(0,0,0,0.1);
        }}
        h2 {{
            color: #667eea;
            margin-bottom: 20px;
            padding-bottom: 10px;
            border-bottom: 2px solid #eee;
        }}
        h3 {{
            color: #764ba2;
            margin: 20px 0 15px 0;
        }}
        .charts-grid {{
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(500px, 1fr));
            gap: 20px;
            margin-bottom: 20px;
        }}
        .chart-container {{
            background: #fafafa;
            padding: 20px;
            border-radius: 8px;
        }}
        canvas {{
            width: 100% !important;
            height: 350px !important;
        }}
        table {{
            width: 100%;
            border-collapse: collapse;
            margin-top: 15px;
        }}
        th, td {{
            padding: 12px 15px;
            text-align: left;
            border-bottom: 1px solid #eee;
        }}
        th {{
            background: #667eea;
            color: white;
            font-weight: 600;
        }}
        tr:hover {{
            background: #f5f5f5;
        }}
        tr:nth-child(even) {{
            background: #fafafa;
        }}
        .best {{
            color: #28a745;
            font-weight: bold;
        }}
        .worst {{
            color: #dc3545;
        }}
        .crashed {{
            color: #999;
            font-style: italic;
        }}
        .status-crash {{
            color: #dc3545;
            font-weight: bold;
        }}
        .status-timeout {{
            color: #fd7e14;
            font-weight: bold;
        }}
        .status-success {{
            color: #28a745;
        }}
        .framework-badge {{
            display: inline-block;
            padding: 4px 10px;
            border-radius: 20px;
            font-size: 0.85rem;
            font-weight: 600;
        }}
        .badge-axum {{ background: #f0d9ff; color: #7b2cbf; }}
        .badge-actix {{ background: #d4edda; color: #155724; }}
        .badge-rocket {{ background: #f8d7da; color: #721c24; }}
        .badge-warp {{ background: #fff3cd; color: #856404; }}
        .badge-tide {{ background: #d1ecf1; color: #0c5460; }}
        .crash-section {{
            background: #fff5f5;
            border-left: 4px solid #dc3545;
            padding: 20px;
            margin-bottom: 20px;
            border-radius: 5px;
        }}
        .crash-item {{
            padding: 15px;
            background: white;
            border-radius: 5px;
            margin-bottom: 10px;
        }}
        .crash-reason {{
            color: #dc3545;
            font-weight: 600;
            margin-bottom: 5px;
        }}
        .crash-details {{
            font-size: 0.9rem;
            color: #666;
        }}
        .no-crashes {{
            color: #28a745;
            font-weight: 600;
        }}
        .category-badge {{
            display: inline-block;
            padding: 2px 8px;
            border-radius: 12px;
            font-size: 0.75rem;
            font-weight: 500;
            background: #e9ecef;
            color: #495057;
        }}
        .category-badge[data-category="JSON Serialization"] {{ background: #fff3cd; color: #856404; }}
        .category-badge[data-category="Memory Allocation"] {{ background: #f0d9ff; color: #7b2cbf; }}
        .category-badge[data-category="I/O"] {{ background: #d1ecf1; color: #0c5460; }}
        .category-badge[data-category="Async Runtime"] {{ background: #d4edda; color: #155724; }}
        .category-badge[data-category="Framework"] {{ background: #f8d7da; color: #721c24; }}
        .category-badge[data-category="Database"] {{ background: #fff0db; color: #9c5c00; }}
        .category-badge[data-category="Template Rendering"] {{ background: #e3f2fd; color: #1565c0; }}
        .category-badge[data-category="Standard Library"] {{ background: #f5f5f5; color: #424242; }}
    </style>
</head>
<body>
    <div class="container">
        <header>
            <h1>🦀 Rust Web Framework Benchmark Report</h1>
            <p>Performance comparison of Axum, Actix-web, Rocket, Warp, and Tide</p>
            <div class="metadata">
                <div class="metadata-item">
                    <div class="metadata-label">Generated</div>
                    <div class="metadata-value">{timestamp}</div>
                </div>
                <div class="metadata-item">
                    <div class="metadata-label">Concurrency</div>
                    <div class="metadata-value">{config_concurrency}</div>
                </div>
                <div class="metadata-item">
                    <div class="metadata-label">Duration</div>
                    <div class="metadata-value">{config_duration}s</div>
                </div>
                <div class="metadata-item">
                    <div class="metadata-label">Runs</div>
                    <div class="metadata-value">{config_repeat}x</div>
                </div>
                <div class="metadata-item">
                    <div class="metadata-label">Frameworks</div>
                    <div class="metadata-value">{frameworks}</div>
                </div>
                <div class="metadata-item">
                    <div class="metadata-label">Timeout</div>
                    <div class="metadata-value">{config_timeout}s</div>
                </div>
            </div>
        </header>

        {crash_section}

        {hotspot_section}

        <div class="section">
            <h2>📊 Benchmark Results by Scenario</h2>
            {charts_html}
        </div>

        <div class="section">
            <h2>📋 Detailed Results</h2>
            {tables_html}
        </div>
    </div>
</body>
</html>"#,
        config_concurrency = report.config.concurrency,
        config_duration = report.config.duration_secs,
        config_repeat = report.config.repeat,
        config_timeout = report.config.timeout_secs,
        frameworks = framework_names.join(", "),
        crash_section = crash_section,
        hotspot_section = hotspot_section,
    )
}

fn generate_crash_section_html(
    crash_info: &[(Framework, RunStatus, Option<&crate::models::CrashInfo>)],
) -> String {
    if crash_info.is_empty() {
        return String::from(
            r#"<div class="section">
            <h2>✅ Run Status</h2>
            <p class="no-crashes">All frameworks completed successfully with no crashes or timeouts.</p>
        </div>"#,
        );
    }

    let mut items = String::new();
    for (framework, status, info) in crash_info {
        let status_class = match status {
            RunStatus::Crash => "status-crash",
            RunStatus::Timeout => "status-timeout",
            _ => "",
        };

        let details = if let Some(ci) = info {
            format!(
                r#"<div class="crash-details">
                    <strong>Reason:</strong> {}<br>
                    <strong>Peak Memory:</strong> {:.1} MB<br>
                    <strong>Timestamp:</strong> {}
                </div>"#,
                ci.reason,
                ci.peak_memory_mb,
                ci.timestamp.format("%Y-%m-%d %H:%M:%S")
            )
        } else {
            String::new()
        };

        items.push_str(&format!(
            r#"<div class="crash-item">
                <div class="framework-badge badge-{badge}">{name}</div>
                <span class="{status_class}"> [{status}]</span>
                {details}
            </div>"#,
            badge = framework_badge_class(*framework),
            name = framework.name(),
            status = status.label(),
            details = details,
            status_class = status_class,
        ));
    }

    format!(
        r#"<div class="section">
            <h2>⚠️ Run Status - {count} Crash(es)/Timeout(s)</h2>
            <div class="crash-section">
                {items}
            </div>
        </div>"#,
        count = crash_info.len(),
        items = items,
    )
}

fn framework_badge_class(f: Framework) -> &'static str {
    match f {
        Framework::Axum => "axum",
        Framework::ActixWeb => "actix",
        Framework::Rocket => "rocket",
        Framework::Warp => "warp",
        Framework::Tide => "tide",
    }
}

fn generate_hotspot_section_html(report: &BenchmarkReport) -> String {
    let mut all_hotspots: HashMap<(Framework, Scenario), HotSpotAnalysis> = HashMap::new();
    
    for result in &report.results {
        if result.status != RunStatus::Success {
            continue;
        }
        for (scenario, profile) in &result.profiles {
            let hotspots = analyze_hotspots(profile);
            all_hotspots.insert((result.framework, *scenario), hotspots);
        }
    }

    if all_hotspots.is_empty() {
        return String::from(
            r#"<div class="section">
            <h2>🔥 CPU Profiling</h2>
            <p>No profile data available. Run with --flamegraph to enable CPU profiling.</p>
        </div>"#,
        );
    }

    let mut html = String::from(
        r#"<div class="section">
        <h2>🔥 CPU Profiling & Hotspot Analysis</h2>
        <p>Flamegraphs are saved in the <code>flamegraphs/</code> directory. Click to view detailed SVG files.</p>
    "#,
    );

    for scenario in &report.config.scenarios {
        let scenario_id = format!("{:?}", scenario).to_lowercase();
        let labels_json = report
            .config
            .targets
            .iter()
            .map(|f| format!("'{}'", f.name()))
            .collect::<Vec<_>>()
            .join(", ");
        let datasets_code = generate_category_datasets_js(report, scenario, &all_hotspots);
        
        html.push_str(&format!(
            r#"<h3>📊 {scenario_name} - Hotspot Comparison</h3>
            <div class="charts-grid">
                <div class="chart-container">
                    <h4>Category Breakdown by CPU Usage</h4>
                    <canvas id="category_{scenario_id}"></canvas>
                </div>
            </div>
            <script>
                (function() {{
                    const labels = [{labels_json}];
                    const datasets = [];
                    const colors = ['#7b2cbf', '#28a745', '#dc3545', '#ffc107', '#17a2b8'];
                    const categories = [
                        'JSON Serialization',
                        'Memory Allocation',
                        'I/O',
                        'Async Runtime',
                        'Framework',
                        'Database',
                        'Template Rendering',
                        'Standard Library',
                        'Other'
                    ];
                    {datasets_code}
                    new Chart(document.getElementById('category_{scenario_id}'), {{
                        type: 'bar',
                        data: {{ labels: categories, datasets: datasets }},
                        options: {{
                            responsive: true,
                            indexAxis: 'y',
                            scales: {{ x: {{ beginAtZero: true, max: 100, title: {{ display: true, text: 'CPU %' }} }} }}
                        }}
                    }});
                }})();
            </script>
        "#,
            scenario_name = scenario.name(),
            scenario_id = scenario_id,
            labels_json = labels_json,
            datasets_code = datasets_code,
        ));

        for framework in &report.config.targets {
            if let Some(hotspots) = all_hotspots.get(&(*framework, *scenario)) {
                html.push_str(&generate_hotspot_table_html(
                    *framework,
                    *scenario,
                    hotspots,
                ));
            }
        }
    }

    html.push_str("</div>");
    html
}

fn generate_category_datasets_js(
    _report: &BenchmarkReport,
    scenario: &Scenario,
    all_hotspots: &HashMap<(Framework, Scenario), HotSpotAnalysis>,
) -> String {
    let mut js = String::new();

    for (i, framework) in _report.config.targets.iter().enumerate() {
        if let Some(hotspots) = all_hotspots.get(&(*framework, *scenario)) {
            let categories = [
                "JSON Serialization",
                "Memory Allocation",
                "I/O",
                "Async Runtime",
                "Framework",
                "Database",
                "Template Rendering",
                "Standard Library",
                "Other",
            ];
            let values: Vec<String> = categories
                .iter()
                .map(|c| format!("{:.2}", hotspots.category_breakdown.get(*c).unwrap_or(&0.0)))
                .collect();
            
            js.push_str(&format!(
                r#"datasets.push({{
                    label: '{}',
                    data: [{}],
                    backgroundColor: colors[{}],
                    borderRadius: 4,
                }});"#,
                framework.name(),
                values.join(", "),
                i
            ));
        }
    }

    js
}

fn generate_hotspot_table_html(
    framework: Framework,
    scenario: Scenario,
    hotspots: &HotSpotAnalysis,
) -> String {
    let badge_class = framework_badge_class(framework);
    let flamegraph_path = format!(
        "flamegraphs/flamegraph_{}_{}.svg",
        framework.name().to_lowercase().replace('-', "_"),
        scenario.name().to_lowercase().replace(' ', "_")
    );

    let mut rows = String::new();
    for (i, func) in hotspots.top_10_functions.iter().enumerate() {
        let category = crate::flamegraph::categorize_function(&format!("{}::{}", func.module, func.name));
        let display_name = if func.name.len() > 60 {
            format!("{}...", &func.name[..57])
        } else {
            func.name.clone()
        };
        
        rows.push_str(&format!(
            r#"<tr>
                <td>{rank}</td>
                <td><code style="font-size: 0.85rem;">{name}</code></td>
                <td><span class="category-badge" data-category="{category}">{category}</span></td>
                <td style="text-align: right; font-weight: bold;">{cpu:.2}%</td>
                <td style="text-align: right;">{calls}</td>
                <td style="text-align: right;">{avg_ns}ns</td>
            </tr>"#,
            rank = i + 1,
            name = html_escape(&display_name),
            category = category,
            cpu = func.cpu_percent,
            calls = func.call_count,
            avg_ns = func.avg_self_time_ns,
        ));
    }

    let total_json: f64 = hotspots
        .json_serialization_functions
        .iter()
        .map(|f| f.cpu_percent)
        .sum();
    let total_alloc: f64 = hotspots
        .memory_alloc_functions
        .iter()
        .map(|f| f.cpu_percent)
        .sum();
    let total_io: f64 = hotspots
        .io_functions
        .iter()
        .map(|f| f.cpu_percent)
        .sum();

    format!(
        r#"<h4>
        <span class="framework-badge badge-{badge_class}">{fw_name}</span>
        <a href="{flamegraph}" target="_blank" style="margin-left: 10px; font-size: 0.9rem;">
            🔥 View Flamegraph →
        </a>
    </h4>
    <div style="display: grid; grid-template-columns: repeat(auto-fit, minmax(150px, 1fr)); gap: 10px; margin: 15px 0;">
        <div class="metadata-item" style="background: #fff3cd; color: #856404;">
            <div class="metadata-label">JSON Overhead</div>
            <div class="metadata-value">{total_json:.1}%</div>
        </div>
        <div class="metadata-item" style="background: #f0d9ff; color: #7b2cbf;">
            <div class="metadata-label">Memory Alloc</div>
            <div class="metadata-value">{total_alloc:.1}%</div>
        </div>
        <div class="metadata-item" style="background: #d1ecf1; color: #0c5460;">
            <div class="metadata-label">I/O Overhead</div>
            <div class="metadata-value">{total_io:.1}%</div>
        </div>
    </div>
    <h5>Top 10 Hot Functions</h5>
    <table>
        <thead>
            <tr>
                <th>#</th>
                <th>Function</th>
                <th>Category</th>
                <th style="text-align: right;">CPU %</th>
                <th style="text-align: right;">Calls</th>
                <th style="text-align: right;">Avg Self (ns)</th>
            </tr>
        </thead>
        <tbody>
            {rows}
        </tbody>
    </table>
    <br>
"#,
        badge_class = badge_class,
        fw_name = framework.name(),
        flamegraph = flamegraph_path,
        total_json = total_json,
        total_alloc = total_alloc,
        total_io = total_io,
        rows = rows,
    )
}

fn html_escape(s: &str) -> String {
    s.replace('&', "&amp;")
        .replace('<', "&lt;")
        .replace('>', "&gt;")
        .replace('"', "&quot;")
}

fn framework_has_crash(
    framework: Framework,
    crash_info: &[(Framework, RunStatus, Option<&crate::models::CrashInfo>)],
) -> bool {
    crash_info.iter().any(|(f, _, _)| *f == framework)
}

fn generate_scenario_charts(scenario: &Scenario, results: &[&AggregatedResult]) -> String {
    let scenario_name = scenario.name();
    let qps_data = results
        .iter()
        .map(|r| r.qps_avg.round() as i64)
        .collect::<Vec<_>>();
    let p50_data = results
        .iter()
        .map(|r| format!("{:.2}", r.latency_avg.p50_ms))
        .collect::<Vec<_>>();
    let p95_data = results
        .iter()
        .map(|r| format!("{:.2}", r.latency_avg.p95_ms))
        .collect::<Vec<_>>();
    let p99_data = results
        .iter()
        .map(|r| format!("{:.2}", r.latency_avg.p99_ms))
        .collect::<Vec<_>>();
    let cpu_data = results
        .iter()
        .map(|r| format!("{:.1}", r.resource_avg.cpu_percent))
        .collect::<Vec<_>>();
    let mem_data = results
        .iter()
        .map(|r| format!("{:.1}", r.resource_avg.memory_mb))
        .collect::<Vec<_>>();
    let labels = results
        .iter()
        .map(|r| r.framework.name().to_string())
        .collect::<Vec<_>>();
    let colors = vec![
        "'#7b2cbf'",
        "'#28a745'",
        "'#dc3545'",
        "'#ffc107'",
        "'#17a2b8'",
    ];

    format!(
        r#"
        <h3>🔬 {scenario_name}</h3>
        <div class="charts-grid">
            <div class="chart-container">
                <h4>QPS (Requests/sec) - Higher is Better</h4>
                <canvas id="qps_{scenario_id}"></canvas>
            </div>
            <div class="chart-container">
                <h4>Latency Distribution (ms) - Lower is Better</h4>
                <canvas id="latency_{scenario_id}"></canvas>
            </div>
            <div class="chart-container">
                <h4>CPU Usage (%) - Lower is Better</h4>
                <canvas id="cpu_{scenario_id}"></canvas>
            </div>
            <div class="chart-container">
                <h4>Memory Usage (MB) - Lower is Better</h4>
                <canvas id="mem_{scenario_id}"></canvas>
            </div>
        </div>
        <script>
            (function() {{
                const labels = [{labels_json}];
                const colors = [{colors_str}];

                new Chart(document.getElementById('qps_{scenario_id}'), {{
                    type: 'bar',
                    data: {{
                        labels: labels,
                        datasets: [{{
                            label: 'QPS',
                            data: [{qps_data}],
                            backgroundColor: colors,
                            borderRadius: 5,
                        }}]
                    }},
                    options: {{
                        responsive: true,
                        plugins: {{ legend: {{ display: false }} }},
                        scales: {{ y: {{ beginAtZero: true }} }}
                    }}
                }});

                new Chart(document.getElementById('latency_{scenario_id}'), {{
                    type: 'bar',
                    data: {{
                        labels: labels,
                        datasets: [
                            {{ label: 'P50', data: [{p50_data}], backgroundColor: '#28a745', borderRadius: 4 }},
                            {{ label: 'P95', data: [{p95_data}], backgroundColor: '#ffc107', borderRadius: 4 }},
                            {{ label: 'P99', data: [{p99_data}], backgroundColor: '#dc3545', borderRadius: 4 }},
                        ]
                    }},
                    options: {{
                        responsive: true,
                        scales: {{ y: {{ beginAtZero: true }} }}
                    }}
                }});

                new Chart(document.getElementById('cpu_{scenario_id}'), {{
                    type: 'bar',
                    data: {{
                        labels: labels,
                        datasets: [{{
                            label: 'CPU %',
                            data: [{cpu_data}],
                            backgroundColor: colors,
                            borderRadius: 5,
                        }}]
                    }},
                    options: {{
                        responsive: true,
                        plugins: {{ legend: {{ display: false }} }},
                        scales: {{ y: {{ beginAtZero: true, max: 100 }} }}
                    }}
                }});

                new Chart(document.getElementById('mem_{scenario_id}'), {{
                    type: 'bar',
                    data: {{
                        labels: labels,
                        datasets: [{{
                            label: 'Memory MB',
                            data: [{mem_data}],
                            backgroundColor: colors,
                            borderRadius: 5,
                        }}]
                    }},
                    options: {{
                        responsive: true,
                        plugins: {{ legend: {{ display: false }} }},
                        scales: {{ y: {{ beginAtZero: true }} }}
                    }}
                }});
            }})();
        </script>
        "#,
        scenario_id = format!("{:?}", scenario).to_lowercase(),
        labels_json = labels.iter().map(|l| format!("'{}'", l)).collect::<Vec<_>>().join(", "),
        colors_str = colors.join(", "),
        qps_data = qps_data.iter().map(|v| v.to_string()).collect::<Vec<_>>().join(", "),
        p50_data = p50_data.join(", "),
        p95_data = p95_data.join(", "),
        p99_data = p99_data.join(", "),
        cpu_data = cpu_data.join(", "),
        mem_data = mem_data.join(", "),
    )
}

fn generate_scenario_table(
    scenario: &Scenario,
    results: &[&AggregatedResult],
    crash_info: &[(Framework, RunStatus, Option<&crate::models::CrashInfo>)],
) -> String {
    let scenario_name = scenario.name();

    let max_qps = results.iter().map(|r| r.qps_avg).fold(f64::NEG_INFINITY, f64::max);
    let min_p99 = results.iter().map(|r| r.latency_avg.p99_ms).fold(f64::INFINITY, f64::min);
    let min_cpu = results.iter().map(|r| r.resource_avg.cpu_percent).fold(f32::INFINITY, f32::min);
    let min_mem = results.iter().map(|r| r.resource_avg.memory_mb).fold(f64::INFINITY, f64::min);

    let mut rows = String::new();

    let all_frameworks = Framework::all();
    for framework in &all_frameworks {
        let badge_class = match framework {
            Framework::Axum => "badge-axum",
            Framework::ActixWeb => "badge-actix",
            Framework::Rocket => "badge-rocket",
            Framework::Warp => "badge-warp",
            Framework::Tide => "badge-tide",
        };

        if let Some(r) = results.iter().find(|r| r.framework == *framework) {
            let qps_class = if (r.qps_avg - max_qps).abs() < 0.001 {
                "best"
            } else {
                ""
            };
            let p99_class = if (r.latency_avg.p99_ms - min_p99).abs() < 0.001 {
                "best"
            } else {
                ""
            };
            let cpu_class = if (r.resource_avg.cpu_percent - min_cpu).abs() < 0.001 {
                "best"
            } else {
                ""
            };
            let mem_class = if (r.resource_avg.memory_mb - min_mem).abs() < 0.001 {
                "best"
            } else {
                ""
            };

            rows.push_str(&format!(
                r#"<tr>
                <td><span class="framework-badge {badge_class}">{fw_name}</span></td>
                <td class="{qps_class}">{qps:.0} ± {qps_std:.0}</td>
                <td>{p50:.2}</td>
                <td>{p95:.2}</td>
                <td class="{p99_class}">{p99:.2}</td>
                <td>{success:.1}%</td>
                <td class="{cpu_class}">{cpu:.1}%</td>
                <td class="{mem_class}">{mem:.1}</td>
            </tr>"#,
                fw_name = r.framework.name(),
                qps = r.qps_avg,
                qps_std = r.qps_std,
                p50 = r.latency_avg.p50_ms,
                p95 = r.latency_avg.p95_ms,
                p99 = r.latency_avg.p99_ms,
                success = r.success_rate_avg * 100.0,
                cpu = r.resource_avg.cpu_percent,
                mem = r.resource_avg.memory_mb,
            ));
        } else if framework_has_crash(*framework, crash_info) {
            rows.push_str(&format!(
                r#"<tr>
                <td><span class="framework-badge {badge_class}">{fw_name}</span></td>
                <td class="crashed" colspan="7">Test failed - framework crashed or timed out</td>
            </tr>"#,
                fw_name = framework.name(),
            ));
        }
    }

    format!(
        r#"
        <h3>📊 {scenario_name}</h3>
        <table>
            <thead>
                <tr>
                    <th>Framework</th>
                    <th>QPS (avg ± std)</th>
                    <th>P50 (ms)</th>
                    <th>P95 (ms)</th>
                    <th>P99 (ms)</th>
                    <th>Success</th>
                    <th>CPU %</th>
                    <th>Memory (MB)</th>
                </tr>
            </thead>
            <tbody>
                {rows}
            </tbody>
        </table>
        <br>
        "#
    )
}

pub fn generate_markdown_report(report: &BenchmarkReport, aggregated: &[AggregatedResult]) -> String {
    let timestamp = report.timestamp.format("%Y-%m-%d %H:%M:%S").to_string();
    let framework_names: Vec<String> = report
        .config
        .targets
        .iter()
        .map(|f| f.name().to_string())
        .collect();

    let crash_info = get_framework_crash_info(&report.results);

    let mut md = format!(
        r#"# 🦀 Rust Web Framework Benchmark Report

Generated: {timestamp}

## Configuration

| Parameter | Value |
|-----------|-------|
| Concurrency | {concurrency} |
| Duration | {duration_secs}s |
| Runs per test | {repeat}x |
| Timeout | {timeout_secs}s |
| Frameworks Tested | {frameworks} |
| Scenarios Tested | {scenarios} |

"#,
        concurrency = report.config.concurrency,
        duration_secs = report.config.duration_secs,
        repeat = report.config.repeat,
        timeout_secs = report.config.timeout_secs,
        frameworks = framework_names.join(", "),
        scenarios = report
            .config
            .scenarios
            .iter()
            .map(|s| s.name())
            .collect::<Vec<_>>()
            .join(", "),
    );

    if !crash_info.is_empty() {
        md.push_str("## ⚠️ Run Status\n\n");
        for (framework, status, info) in &crash_info {
            md.push_str(&format!(
                "- **{}**: `{}`",
                framework.name(),
                status.label()
            ));
            if let Some(ci) = info {
                md.push_str(&format!(
                    " - Reason: {} | Peak Memory: {:.1} MB | {}\n",
                    ci.reason,
                    ci.peak_memory_mb,
                    ci.timestamp.format("%Y-%m-%d %H:%M:%S")
                ));
            } else {
                md.push('\n');
            }
        }
        md.push('\n');
    } else {
        md.push_str("## ✅ Run Status\n\nAll frameworks completed successfully with no crashes or timeouts.\n\n");
    }

    if report.config.profile.enabled {
        md.push_str("## 🔥 CPU Profiling & Hotspot Analysis\n\n");
        md.push_str("Flamegraphs are saved in the `flamegraphs/` directory.\n\n");

        let mut all_hotspots: HashMap<(Framework, Scenario), HotSpotAnalysis> = HashMap::new();
        for result in &report.results {
            if result.status != RunStatus::Success {
                continue;
            }
            for (scenario, profile) in &result.profiles {
                let hotspots = analyze_hotspots(profile);
                all_hotspots.insert((result.framework, *scenario), hotspots);
            }
        }

        for scenario in &report.config.scenarios {
            md.push_str(&format!("### {} - Hotspot Summary\n\n", scenario.name()));

            for framework in &report.config.targets {
                if let Some(hotspots) = all_hotspots.get(&(*framework, *scenario)) {
                    let total_json: f64 = hotspots
                        .json_serialization_functions
                        .iter()
                        .map(|f| f.cpu_percent)
                        .sum();
                    let total_alloc: f64 = hotspots
                        .memory_alloc_functions
                        .iter()
                        .map(|f| f.cpu_percent)
                        .sum();
                    let total_io: f64 = hotspots
                        .io_functions
                        .iter()
                        .map(|f| f.cpu_percent)
                        .sum();

                    md.push_str(&format!(
                        "#### {}\n\n- JSON Overhead: **{:.1}%** | Memory Alloc: **{:.1}%** | I/O: **{:.1}%**\n\n",
                        framework.name(),
                        total_json,
                        total_alloc,
                        total_io
                    ));

                    md.push_str("| # | Function | Category | CPU % | Calls |\n");
                    md.push_str("|---|----------|----------|-------|-------|\n");
                    for (i, func) in hotspots.top_10_functions.iter().take(5).enumerate() {
                        let category = crate::flamegraph::categorize_function(&format!("{}::{}", func.module, func.name));
                        let display_name = if func.name.len() > 50 {
                            format!("{}...", &func.name[..47])
                        } else {
                            func.name.clone()
                        };
                        md.push_str(&format!(
                            "| {} | `{}` | {} | {:.2}% | {} |\n",
                            i + 1,
                            display_name.replace('|', "\\|"),
                            category,
                            func.cpu_percent,
                            func.call_count
                        ));
                    }
                    md.push('\n');
                }
            }
        }
    }

    for scenario in &report.config.scenarios {
        let scenario_results: Vec<&AggregatedResult> = aggregated
            .iter()
            .filter(|r| r.scenario == *scenario)
            .collect();

        md.push_str(&format!(
            r#"## 🔬 {}

| Framework | QPS (avg ± std) | P50 (ms) | P95 (ms) | P99 (ms) | Success | CPU % | Memory (MB) |
|-----------|-----------------|----------|----------|----------|---------|-------|-------------|
"#,
            scenario.name()
        ));

        let mut sorted_results = scenario_results.clone();
        sorted_results.sort_by(|a, b| b.qps_avg.partial_cmp(&a.qps_avg).unwrap());

        for r in &sorted_results {
            md.push_str(&format!(
                "| {} | {:.0} ± {:.0} | {:.2} | {:.2} | {:.2} | {:.1}% | {:.1}% | {:.1} |\n",
                r.framework.name(),
                r.qps_avg,
                r.qps_std,
                r.latency_avg.p50_ms,
                r.latency_avg.p95_ms,
                r.latency_avg.p99_ms,
                r.success_rate_avg * 100.0,
                r.resource_avg.cpu_percent,
                r.resource_avg.memory_mb,
            ));
        }

        for (framework, status, _) in &crash_info {
            if !sorted_results.iter().any(|r| r.framework == *framework) {
                md.push_str(&format!(
                    "| {} | - | - | - | - | - | - | - | ({}) |\n",
                    framework.name(),
                    status.label()
                ));
            }
        }

        md.push('\n');
    }

    md.push_str(
        r#"## Notes

- All tests run in release mode with `opt-level=3`, `lto=fat`, `codegen-units=1`
- QPS = Queries Per Second, higher is better
- P50/P95/P99 = Latency percentiles, lower is better
- CPU and Memory usage are averages sampled during the test
- Timeout: Each framework test is limited to the configured timeout (default 300s)
- If a framework crashes or times out, it is marked in the results and skipped
"#,
    );

    md
}
