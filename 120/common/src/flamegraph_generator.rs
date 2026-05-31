use crate::flamegraph::{FunctionDiff, ProfileData, ProfileDiff};
use std::collections::HashMap;

struct FrameNode {
    name: String,
    module: String,
    value: u64,
    children: HashMap<String, FrameNode>,
}

impl FrameNode {
    fn new(name: String, module: String) -> Self {
        FrameNode {
            name,
            module,
            value: 0,
            children: HashMap::new(),
        }
    }

    fn add_value(&mut self, value: u64) {
        self.value += value;
    }
}

pub struct FlamegraphGenerator {
    width: u32,
    height: u32,
    frame_height: u32,
    color_palette: Vec<String>,
}

impl FlamegraphGenerator {
    pub fn new() -> Self {
        FlamegraphGenerator {
            width: 1200,
            height: 400,
            frame_height: 16,
            color_palette: generate_heat_palette(),
        }
    }

    pub fn generate_svg(&self, profile: &ProfileData) -> String {
        let root = self.build_tree(profile);
        let total_value = root.value;

        let mut svg = String::new();
        svg.push_str(&self.svg_header());

        let title = format!(
            "{} - {} ({} samples, {}s)",
            profile.framework,
            profile.scenario,
            profile.sample_count,
            profile.duration_secs
        );

        let title_x = (self.width / 2) as f64;
        svg.push_str("<text x=\"");
        svg.push_str(&title_x.to_string());
        svg.push_str("\" y=\"20\" font-size=\"16\" font-weight=\"bold\" text-anchor=\"middle\" fill=\"#333\">");
        svg.push_str(&title);
        svg.push_str("</text>");

        if total_value > 0 {
            svg.push_str(&self.render_tree(&root, 0.0, 0, total_value, profile));
        } else {
            let text_x = (self.width / 2) as f64;
            let text_y = (self.height / 2) as f64;
            svg.push_str("<text x=\"");
            svg.push_str(&text_x.to_string());
            svg.push_str("\" y=\"");
            svg.push_str(&text_y.to_string());
            svg.push_str("\" font-size=\"14\" text-anchor=\"middle\" fill=\"#999\">No profile data available</text>");
        }

        svg.push_str(&self.svg_footer());
        svg
    }

    fn build_tree(&self, profile: &ProfileData) -> FrameNode {
        let mut root = FrameNode::new("root".to_string(), "root".to_string());

        for sample in &profile.samples {
            let weight = sample.weight;
            let mut current = &mut root;

            for frame in &sample.frames {
                let key = format!("{}::{}", frame.module, frame.function);
                if !current.children.contains_key(&key) {
                    current.children.insert(
                        key.clone(),
                        FrameNode::new(frame.function.clone(), frame.module.clone()),
                    );
                }
                current = current.children.get_mut(&key).unwrap();
                current.add_value(weight);
            }
        }

        let total: u64 = root.children.values().map(|c| c.value).sum();
        root.value = total;

        root
    }

    fn render_tree(
        &self,
        node: &FrameNode,
        x: f64,
        level: u32,
        total: u64,
        profile: &ProfileData,
    ) -> String {
        let mut svg = String::new();

        if node.name == "root" {
            for child in node.children.values() {
                svg.push_str(&self.render_node(child, x, level, total, profile));
            }
            return svg;
        }

        svg.push_str(&self.render_node(node, x, level, total, profile));
        svg
    }

    fn render_node(
        &self,
        node: &FrameNode,
        mut x: f64,
        level: u32,
        total: u64,
        _profile: &ProfileData,
    ) -> String {
        let mut svg = String::new();

        let width = (node.value as f64 / total as f64) * self.width as f64;
        let y = self.height as f64 - (level + 1) as f64 * self.frame_height as f64;

        let cpu_percent = (node.value as f64 / total as f64) * 100.0;
        let color_index = if cpu_percent * 100.0 > (self.color_palette.len() - 1) as f64 {
            self.color_palette.len() - 1
        } else {
            (cpu_percent * 10.0) as usize
        };
        let color = &self.color_palette[color_index.min(self.color_palette.len() - 1)];

        let display_name = if node.name.len() > 40 {
            format!("{}...", &node.name[..37])
        } else {
            node.name.clone()
        };

        let title_text = format!(
            "{}::{} - {:.2}% ({}/{} samples)",
            node.module,
            node.name,
            (node.value as f64 / total as f64) * 100.0,
            node.value,
            total
        );

        let x_text = x + 3.0;
        let y_text = y + self.frame_height as f64 - 4.0;
        let rect_width = if width > 1.0 { width } else { 1.0 };
        let rect_height = self.frame_height - 1;
        let text_content = if width > 50.0 { display_name } else { String::new() };

        svg.push_str("<g class=\"frame\" onclick=\"selectFrame(this)\">");
        svg.push_str("<title>");
        svg.push_str(&title_text);
        svg.push_str("</title>");
        svg.push_str("<rect x=\"");
        svg.push_str(&format!("{:.2}", x));
        svg.push_str("\" y=\"");
        svg.push_str(&format!("{:.2}", y));
        svg.push_str("\" width=\"");
        svg.push_str(&format!("{:.2}", rect_width));
        svg.push_str("\" height=\"");
        svg.push_str(&rect_height.to_string());
        svg.push_str("\" fill=\"");
        svg.push_str(color);
        svg.push_str("\" stroke=\"#fff\" stroke-width=\"0.5\"");
        svg.push_str(" onmouseover=\"highlight(this)\" onmouseout=\"unhighlight(this)\"/>");
        svg.push_str("<text x=\"");
        svg.push_str(&format!("{:.2}", x_text));
        svg.push_str("\" y=\"");
        svg.push_str(&format!("{:.2}", y_text));
        svg.push_str("\" font-size=\"11\" font-family=\"monospace\"");
        svg.push_str(" fill=\"#000\" pointer-events=\"none\">");
        svg.push_str(&text_content);
        svg.push_str("</text>");
        svg.push_str("</g>");

        let mut children: Vec<&FrameNode> = node.children.values().collect();
        children.sort_by(|a, b| b.value.cmp(&a.value));

        for child in children {
            svg.push_str(&self.render_node(child, x, level + 1, total, _profile));
            x += (child.value as f64 / total as f64) * self.width as f64;
        }

        svg
    }

    fn svg_header(&self) -> String {
        let mut svg = String::new();
        
        svg.push_str("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"");
        svg.push_str(&self.width.to_string());
        svg.push_str("\" height=\"");
        svg.push_str(&self.height.to_string());
        svg.push_str("\" viewBox=\"0 0 ");
        svg.push_str(&self.width.to_string());
        svg.push_str(" ");
        svg.push_str(&self.height.to_string());
        svg.push_str("\">");
        
        svg.push_str("\n            <defs>\n                <style>\n                    .frame { cursor: pointer; }\n                    .frame:hover rect { filter: brightness(1.1); }\n                    .frame.selected rect { stroke: #333; stroke-width: 2; }\n                </style>\n            </defs>\n            <rect width=\"100%\" height=\"100%\" fill=\"#fafafa\"/>\n            <script>\n                function highlight(el) {\n                    el.querySelector('rect').style.filter = 'brightness(1.2)';\n                }\n                function unhighlight(el) {\n                    el.querySelector('rect').style.filter = '';\n                }\n                function selectFrame(el) {\n                    document.querySelectorAll('.frame.selected').forEach(f => f.classList.remove('selected'));\n                    el.classList.add('selected');\n                    console.log(el.querySelector('title').textContent);\n                }\n            </script>\n");
        
        svg
    }

    fn svg_footer(&self) -> String {
        String::from("</svg>")
    }
}

impl Default for FlamegraphGenerator {
    fn default() -> Self {
        Self::new()
    }
}

fn generate_heat_palette() -> Vec<String> {
    let mut palette = Vec::new();
    
    for i in 0..=100 {
        let ratio = i as f64 / 100.0;
        let r = (34.0 + ratio * 180.0) as u8;
        let g = (193.0 - ratio * 130.0) as u8;
        let b = (73.0 + ratio * 20.0) as u8;
        palette.push(format!("#{:02x}{:02x}{:02x}", r, g, b));
    }
    
    palette
}

pub fn generate_diff_report(diff: &ProfileDiff) -> String {
    let mut report = String::new();
    
    report.push_str(&format!(
        "## Performance Diff: {} vs {}\n\n",
        diff.framework_a,
        diff.framework_b
    ));
    
    report.push_str("### Category Differences\n\n");
    report.push_str("| Category | CPU % (A) | CPU % (B) | Diff |\n");
    report.push_str("|----------|-----------|-----------|------|\n");
    
    let mut sorted_cats: Vec<(&String, &f64)> = diff.category_diffs.iter().collect();
    sorted_cats.sort_by(|a, b| b.1.abs().partial_cmp(&a.1.abs()).unwrap());
    
    for (cat, diff_val) in sorted_cats {
        let sign = if *diff_val > 0.0 { "+" } else { "" };
        report.push_str(&format!(
            "| {} | - | - | {}{:.2}% |\n",
            cat, sign, diff_val
        ));
    }
    
    report.push_str("\n### Top Function Differences\n\n");
    report.push_str("| Function | CPU % (A) | CPU % (B) | Diff | Significance |\n");
    report.push_str("|----------|-----------|-----------|------|--------------|\n");
    
    for func in &diff.function_diffs {
        let sign = if func.diff_percent > 0.0 { "+" } else { "" };
        report.push_str(&format!(
            "| {} | {:.2}% | {:.2}% | {}{:.2}% | {:.2} |\n",
            func.function,
            func.cpu_percent_a,
            func.cpu_percent_b,
            sign,
            func.diff_percent,
            func.significance
        ));
    }
    
    report.push_str(&format!(
        "\n### Bottlenecks in {}\n\n",
        diff.framework_a
    ));
    for func in &diff.bottlenecks_a {
        report.push_str(&format!(
            "- `{}` - {:.2}% CPU\n",
            func.name,
            func.cpu_percent
        ));
    }
    
    report.push_str(&format!(
        "\n### Bottlenecks in {}\n\n",
        diff.framework_b
    ));
    for func in &diff.bottlenecks_b {
        report.push_str(&format!(
            "- `{}` - {:.2}% CPU\n",
            func.name,
            func.cpu_percent
        ));
    }
    
    report
}

pub fn compare_profiles(a: &ProfileData, b: &ProfileData) -> ProfileDiff {
    let mut function_diffs = Vec::new();
    let mut all_functions: HashMap<String, (f64, f64)> = HashMap::new();

    for (name, func) in &a.functions {
        all_functions.insert(name.clone(), (func.cpu_percent, 0.0));
    }
    for (name, func) in &b.functions {
        let entry = all_functions.entry(name.clone()).or_insert((0.0, 0.0));
        entry.1 = func.cpu_percent;
    }

    for (name, (cpu_a, cpu_b)) in &all_functions {
        if *cpu_a > 0.5 || *cpu_b > 0.5 {
            let diff = cpu_b - cpu_a;
            let significance = diff.abs() / (cpu_a.max(*cpu_b).max(0.1));
            
            let call_count_a = a.functions.get(name).map(|f| f.call_count).unwrap_or(0);
            let call_count_b = b.functions.get(name).map(|f| f.call_count).unwrap_or(0);

            function_diffs.push(FunctionDiff {
                function: name.clone(),
                cpu_percent_a: *cpu_a,
                cpu_percent_b: *cpu_b,
                diff_percent: diff,
                call_count_a,
                call_count_b,
                significance,
            });
        }
    }

    function_diffs.sort_by(|a, b| b.significance.partial_cmp(&a.significance).unwrap());

    let mut category_diffs: HashMap<String, f64> = HashMap::new();
    for category in [
        "JSON Serialization",
        "Memory Allocation",
        "I/O",
        "Async Runtime",
        "Framework",
        "Database",
        "Template Rendering",
        "Standard Library",
        "Other",
    ] {
        let a_total: f64 = a
            .functions
            .iter()
            .filter(|(n, _)| crate::flamegraph::categorize_function(n) == category)
            .map(|(_, f)| f.cpu_percent)
            .sum();
        let b_total: f64 = b
            .functions
            .iter()
            .filter(|(n, _)| crate::flamegraph::categorize_function(n) == category)
            .map(|(_, f)| f.cpu_percent)
            .sum();
        category_diffs.insert(category.to_string(), b_total - a_total);
    }

    let mut bottlenecks_a: Vec<_> = a
        .top_functions
        .iter()
        .filter(|f| f.cpu_percent > 5.0)
        .cloned()
        .collect();
    bottlenecks_a.sort_by(|a, b| b.cpu_percent.partial_cmp(&a.cpu_percent).unwrap());
    bottlenecks_a.truncate(5);

    let mut bottlenecks_b: Vec<_> = b
        .top_functions
        .iter()
        .filter(|f| f.cpu_percent > 5.0)
        .cloned()
        .collect();
    bottlenecks_b.sort_by(|a, b| b.cpu_percent.partial_cmp(&a.cpu_percent).unwrap());
    bottlenecks_b.truncate(5);

    ProfileDiff {
        framework_a: a.framework.clone(),
        framework_b: b.framework.clone(),
        scenario: a.scenario.clone(),
        function_diffs,
        category_diffs,
        bottlenecks_a,
        bottlenecks_b,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::flamegraph::{ProfileData, StackSample, StackFrame, FunctionProfile};
    use std::collections::HashMap;

    fn create_test_profile(framework: &str, scenario: &str) -> ProfileData {
        let samples = vec![
            StackSample {
                timestamp_ns: 0,
                tid: 1,
                frames: vec![
                    StackFrame { function: "main".to_string(), module: "test".to_string(), offset: 0 },
                    StackFrame { function: "handle_request".to_string(), module: "axum".to_string(), offset: 0 },
                    StackFrame { function: "serde_json::to_string".to_string(), module: "serde_json".to_string(), offset: 0 },
                ],
                weight: 10,
            },
            StackSample {
                timestamp_ns: 100,
                tid: 1,
                frames: vec![
                    StackFrame { function: "main".to_string(), module: "test".to_string(), offset: 0 },
                    StackFrame { function: "handle_request".to_string(), module: "axum".to_string(), offset: 0 },
                    StackFrame { function: "alloc::alloc::alloc".to_string(), module: "alloc".to_string(), offset: 0 },
                ],
                weight: 5,
            },
        ];

        let mut functions = HashMap::new();
        functions.insert(
            "serde_json::to_string".to_string(),
            FunctionProfile {
                name: "serde_json::to_string".to_string(),
                module: "serde_json".to_string(),
                cpu_percent: 30.0,
                call_count: 100,
                avg_self_time_ns: 1000,
                total_time_ns: 100000,
                children: Vec::new(),
            },
        );
        functions.insert(
            "alloc::alloc::alloc".to_string(),
            FunctionProfile {
                name: "alloc::alloc::alloc".to_string(),
                module: "alloc".to_string(),
                cpu_percent: 15.0,
                call_count: 200,
                avg_self_time_ns: 500,
                total_time_ns: 50000,
                children: Vec::new(),
            },
        );

        let top_functions: Vec<FunctionProfile> = functions.values().cloned().collect();

        ProfileData {
            framework: framework.to_string(),
            scenario: scenario.to_string(),
            sample_count: 15,
            sampling_freq_hz: 99,
            duration_secs: 5,
            samples,
            functions,
            top_functions,
        }
    }

    #[test]
    fn test_flamegraph_svg_generation() {
        let profile = create_test_profile("Axum", "JSON Serialization");
        let generator = FlamegraphGenerator::new();
        let svg = generator.generate_svg(&profile);

        assert!(svg.contains("<svg"));
        assert!(svg.contains("</svg>"));
        assert!(svg.contains("Axum"));
        assert!(svg.contains("JSON Serialization"));
        assert!(svg.contains("serde_json::to_string"));
        assert!(svg.contains("<rect"));
        assert!(svg.contains("<text"));
    }

    #[test]
    fn test_profile_comparison() {
        let profile_a = create_test_profile("Axum", "JSON Serialization");
        let profile_b = create_test_profile("Actix-web", "JSON Serialization");

        let diff = compare_profiles(&profile_a, &profile_b);

        assert_eq!(diff.framework_a, "Axum");
        assert_eq!(diff.framework_b, "Actix-web");
        assert_eq!(diff.scenario, "JSON Serialization");
        assert!(!diff.function_diffs.is_empty());
        assert!(!diff.category_diffs.is_empty());
    }

    #[test]
    fn test_diff_report_generation() {
        let profile_a = create_test_profile("Axum", "JSON Serialization");
        let profile_b = create_test_profile("Actix-web", "JSON Serialization");
        let diff = compare_profiles(&profile_a, &profile_b);

        let report = generate_diff_report(&diff);

        assert!(report.contains("Performance Diff"));
        assert!(report.contains("Axum"));
        assert!(report.contains("Actix-web"));
        assert!(report.contains("Category Differences"));
        assert!(report.contains("Top Function Differences"));
    }

    #[test]
    fn test_flamegraph_generator_default() {
        let generator = FlamegraphGenerator::default();
        assert_eq!(generator.width, 1200);
        assert_eq!(generator.height, 400);
        assert_eq!(generator.frame_height, 16);
    }

    #[test]
    fn test_heat_palette_generation() {
        let palette = generate_heat_palette();
        assert_eq!(palette.len(), 101);
        assert!(palette[0].starts_with("#"));
        assert!(palette[100].starts_with("#"));
    }
}
