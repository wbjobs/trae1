use std::collections::HashMap;
use std::fs;
use std::path::{Path, PathBuf};

use serde::{Deserialize, Serialize};

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct TrajectoryPoint {
    pub label: String,
    pub label_index: usize,
    pub x: f32,
    pub y: f32,
    pub confidence: f32,
    pub timestamp: f64,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ActionStep {
    pub label: String,
    pub label_index: usize,
    pub x_range: (f32, f32),
    pub y_range: (f32, f32),
    pub min_confidence: f32,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ActionSequence {
    pub id: String,
    pub name: String,
    pub steps: Vec<ActionStep>,
    pub trigger: ActionTrigger,
    pub match_threshold: f32,
    pub step_timeout_ms: f64,
    pub created_at: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
#[serde(tag = "type")]
pub enum ActionTrigger {
    #[serde(rename = "open_app")]
    OpenApp { path: String },
    #[serde(rename = "shell_command")]
    ShellCommand { command: String, args: Vec<String> },
    #[serde(rename = "shortcut")]
    Shortcut { keys: String },
}

#[derive(Debug, Clone, Serialize)]
pub struct ActionMatchResult {
    pub sequence_id: String,
    pub sequence_name: String,
    pub score: f32,
    pub step_scores: Vec<f32>,
}

fn sequences_file(app: &tauri::AppHandle) -> Result<PathBuf, String> {
    let dir = app
        .path()
        .resolve("actions", tauri::path::BaseDirectory::AppData)
        .map_err(|e| e.to_string())?;
    fs::create_dir_all(&dir).map_err(|e| e.to_string())?;
    Ok(dir.join("sequences.json"))
}

pub fn load_sequences(app: &tauri::AppHandle) -> Result<Vec<ActionSequence>, String> {
    let path = sequences_file(app)?;
    if !path.exists() {
        return Ok(Vec::new());
    }
    let data = fs::read_to_string(&path).map_err(|e| e.to_string())?;
    if data.trim().is_empty() {
        return Ok(Vec::new());
    }
    serde_json::from_str(&data).map_err(|e| e.to_string())
}

pub fn save_sequences(
    app: &tauri::AppHandle,
    sequences: &[ActionSequence],
) -> Result<(), String> {
    let path = sequences_file(app)?;
    let data = serde_json::to_string_pretty(sequences).map_err(|e| e.to_string())?;
    fs::write(&path, data).map_err(|e| e.to_string())?;
    Ok(())
}

pub fn match_sequences(
    trajectory: &[TrajectoryPoint],
    sequences: &[ActionSequence],
) -> Vec<ActionMatchResult> {
    let mut results = Vec::new();

    for seq in sequences {
        if let Some(score_info) = match_sequence(trajectory, seq) {
            results.push(ActionMatchResult {
                sequence_id: seq.id.clone(),
                sequence_name: seq.name.clone(),
                score: score_info.0,
                step_scores: score_info.1,
            });
        }
    }

    results.sort_by(|a, b| b.score.partial_cmp(&a.score).unwrap_or(std::cmp::Ordering::Equal));
    results
}

fn match_sequence(
    trajectory: &[TrajectoryPoint],
    sequence: &ActionSequence,
) -> Option<(f32, Vec<f32>)> {
    if trajectory.is_empty() || sequence.steps.is_empty() {
        return None;
    }

    let mut step_scores: Vec<f32> = Vec::new();
    let mut traj_idx = 0;

    for step in &sequence.steps {
        let mut best_match: Option<(f32, usize)> = None;

        let t_start = if traj_idx > 0 {
            trajectory[traj_idx - 1].timestamp
        } else {
            trajectory[0].timestamp
        };

        while traj_idx < trajectory.len() {
            let point = &trajectory[traj_idx];

            if point.timestamp - t_start > sequence.step_timeout_ms / 1000.0 {
                break;
            }

            let label_match = if point.label == step.label {
                1.0
            } else {
                0.0
            };

            let x_in_range = if point.x >= step.x_range.0 && point.x <= step.x_range.1 {
                1.0
            } else {
                let dist = if point.x < step.x_range.0 {
                    step.x_range.0 - point.x
                } else {
                    point.x - step.x_range.1
                };
                (1.0 - dist.min(1.0)).max(0.0)
            };

            let y_in_range = if point.y >= step.y_range.0 && point.y <= step.y_range.1 {
                1.0
            } else {
                let dist = if point.y < step.y_range.0 {
                    step.y_range.0 - point.y
                } else {
                    point.y - step.y_range.1
                };
                (1.0 - dist.min(1.0)).max(0.0)
            };

            let conf_ok = if point.confidence >= step.min_confidence {
                1.0
            } else {
                point.confidence / step.min_confidence.max(0.01)
            };

            let step_score =
                0.4 * label_match + 0.25 * x_in_range + 0.2 * y_in_range + 0.15 * conf_ok;

            match best_match {
                None => {
                    best_match = Some((step_score, traj_idx));
                }
                Some((best, _)) => {
                    if step_score > best {
                        best_match = Some((step_score, traj_idx));
                    }
                }
            }

            if label_match >= 0.99
                && x_in_range >= 0.99
                && point.confidence >= step.min_confidence
            {
                break;
            }

            traj_idx += 1;
        }

        match best_match {
            Some((score, idx)) => {
                step_scores.push(score);
                traj_idx = idx + 1;
            }
            None => {
                return None;
            }
        }
    }

    if step_scores.is_empty() {
        return None;
    }

    let avg_score = step_scores.iter().sum::<f32>() / step_scores.len() as f32;

    if avg_score >= sequence.match_threshold {
        Some((avg_score, step_scores))
    } else {
        None
    }
}

pub fn simplify_trajectory(
    points: &[TrajectoryPoint],
    min_distance: f32,
) -> Vec<TrajectoryPoint> {
    if points.is_empty() {
        return Vec::new();
    }

    let mut simplified = vec![points[0].clone()];

    for point in &points[1..] {
        let last = simplified.last().unwrap();
        let dx = point.x - last.x;
        let dy = point.y - last.y;
        let dist = (dx * dx + dy * dy).sqrt();

        if dist >= min_distance || point.label != last.label {
            simplified.push(point.clone());
        }
    }

    simplified
}

pub fn direction_from_trajectory(points: &[TrajectoryPoint]) -> String {
    if points.len() < 2 {
        return "static".to_string();
    }

    let first = points.first().unwrap();
    let last = points.last().unwrap();

    let dx = last.x - first.x;
    let dy = last.y - first.y;

    if dx.abs() < 0.1 && dy.abs() < 0.1 {
        return "static".to_string();
    }

    if dx.abs() > dy.abs() {
        if dx > 0.0 {
            "right".to_string()
        } else {
            "left".to_string()
        }
    } else {
        if dy > 0.0 {
            "down".to_string()
        } else {
            "up".to_string()
        }
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct SequenceTemplate {
    pub name: String,
    pub steps: Vec<ActionStep>,
    pub trigger: ActionTrigger,
}

pub fn create_sequence_from_template(
    template: SequenceTemplate,
    threshold: Option<f32>,
    timeout_ms: Option<f64>,
) -> ActionSequence {
    ActionSequence {
        id: uuid(),
        name: template.name,
        steps: template.steps,
        trigger: template.trigger,
        match_threshold: threshold.unwrap_or(0.6),
        step_timeout_ms: timeout_ms.unwrap_or(3000.0),
        created_at: chrono_like_now(),
    }
}

fn uuid() -> String {
    use std::time::{SystemTime, UNIX_EPOCH};
    let nanos = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_nanos())
        .unwrap_or(0);
    format!("seq-{nanos:x}")
}

fn chrono_like_now() -> String {
    let secs = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_secs())
        .unwrap_or(0);
    let days = secs / 86400;
    let years_approx = 1970 + days / 365;
    format!("{years_approx}-01-01T00:00:00Z")
}

pub fn default_sequences() -> Vec<SequenceTemplate> {
    vec![
        SequenceTemplate {
            name: "V字→点赞 打开计算器".into(),
            steps: vec![
                ActionStep {
                    label: "v_sign".into(),
                    label_index: 2,
                    x_range: (0.0, 0.4),
                    y_range: (0.0, 1.0),
                    min_confidence: 0.6,
                },
                ActionStep {
                    label: "like".into(),
                    label_index: 0,
                    x_range: (0.6, 1.0),
                    y_range: (0.0, 1.0),
                    min_confidence: 0.6,
                },
            ],
            trigger: ActionTrigger::OpenApp {
                path: "calc.exe".into(),
            },
        },
        SequenceTemplate {
            name: "手掌→拳头 截图".into(),
            steps: vec![
                ActionStep {
                    label: "palm".into(),
                    label_index: 4,
                    x_range: (0.0, 1.0),
                    y_range: (0.0, 1.0),
                    min_confidence: 0.5,
                },
                ActionStep {
                    label: "fist".into(),
                    label_index: 1,
                    x_range: (0.0, 1.0),
                    y_range: (0.0, 1.0),
                    min_confidence: 0.5,
                },
            ],
            trigger: ActionTrigger::Shortcut {
                keys: "Win+Shift+S".into(),
            },
        },
        SequenceTemplate {
            name: "OK手势 打开记事本".into(),
            steps: vec![ActionStep {
                label: "ok".into(),
                label_index: 3,
                x_range: (0.0, 1.0),
                y_range: (0.0, 1.0),
                min_confidence: 0.7,
            }],
            trigger: ActionTrigger::OpenApp {
                path: "notepad.exe".into(),
            },
        },
    ]
}
