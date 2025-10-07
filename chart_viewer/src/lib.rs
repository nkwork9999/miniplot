use iced::{
    Element, Theme, Length, Task,
    widget::{canvas, column, container, text},
};
use iced::widget::canvas::{Frame, Geometry, Path, Stroke};
use iced::{Color, Point, Rectangle, Size, Pixels};
use std::ffi::CStr;
use std::os::raw::c_char;

#[derive(Default, Clone)]
pub struct ChartData {
    pub title: String,
    pub x_data: Vec<String>,
    pub y_data: Vec<f64>,
    pub chart_type: String,
}

pub struct ChartApp {
    data: ChartData,
}

#[derive(Debug, Clone)]
pub enum Message {}

impl ChartApp {
    pub fn new(data: ChartData) -> (Self, Task<Message>) {
        (ChartApp { data }, Task::none())
    }

    pub fn title(&self) -> String {
        let chart_type_title = if !self.data.chart_type.is_empty() {
            let mut chars = self.data.chart_type.chars();
            match chars.next() {
                None => String::new(),
                Some(first) => first.to_uppercase().collect::<String>() + chars.as_str(),
            }
        } else {
            "Chart".to_string()
        };
        
        format!("DuckDB {} - {}", chart_type_title, self.data.title)
    }

    pub fn update(&mut self, _message: Message) -> Task<Message> {
        Task::none()
    }

    pub fn view(&self) -> Element<Message> {
        let chart = canvas(self as &Self)
            .width(Length::Fill)
            .height(Length::Fill);

        container(
            column![
                text(&self.data.title).size(24),
                chart,
            ]
            .spacing(10)
        )
        .padding(20)
        .into()
    }

    pub fn run(data: ChartData) -> iced::Result {
        iced::application(
            Self::title,
            Self::update,
            Self::view
        )
        .window(iced::window::Settings {
            size: Size::new(800.0, 600.0),
            position: iced::window::Position::Centered,
            ..Default::default()
        })
        .run_with(|| Self::new(data))
    }
}

// macOSのGCDのFFI定義
#[cfg(target_os = "macos")]
extern "C" {
    fn dispatch_get_main_queue() -> *mut std::ffi::c_void;
    fn dispatch_async_f(
        queue: *mut std::ffi::c_void,
        context: *mut std::ffi::c_void,
        work: extern "C" fn(*mut std::ffi::c_void),
    );
}

// メインスレッドで実行される関数
#[cfg(target_os = "macos")]
extern "C" fn run_chart_on_main(context: *mut std::ffi::c_void) {
    unsafe {
        let data = Box::from_raw(context as *mut ChartData);
        
        // メインスレッドなのでIcedを起動できる！
        let _ = ChartApp::run(*data);
    }
}

// FFI: C++から呼ばれる関数
#[no_mangle]
pub extern "C" fn chart_viewer_show_chart(
    title: *const c_char,
    x_data_json: *const c_char,
    y_data_json: *const c_char,
    chart_type: *const c_char,
) -> i32 {
    unsafe {
        let title = match CStr::from_ptr(title).to_str() {
            Ok(s) => s.to_string(),
            Err(_) => return -1,
        };
        
        let x_json = match CStr::from_ptr(x_data_json).to_str() {
            Ok(s) => s,
            Err(_) => return -1,
        };
        
        let y_json = match CStr::from_ptr(y_data_json).to_str() {
            Ok(s) => s,
            Err(_) => return -1,
        };
        
        let chart_type = match CStr::from_ptr(chart_type).to_str() {
            Ok(s) => s.to_string(),
            Err(_) => return -1,
        };
        
        let x_data: Vec<String> = match serde_json::from_str(x_json) {
            Ok(data) => data,
            Err(_) => return -1,
        };
        
        let y_data: Vec<f64> = match serde_json::from_str(y_json) {
            Ok(data) => data,
            Err(_) => return -1,
        };
        
        let data = ChartData {
            title,
            x_data,
            y_data,
            chart_type,
        };
        
        #[cfg(target_os = "macos")]
        {
            // メインスレッドにタスクを投げる
            let queue = dispatch_get_main_queue();
            let context = Box::into_raw(Box::new(data));
            
            dispatch_async_f(
                queue,
                context as *mut std::ffi::c_void,
                run_chart_on_main,
            );
        }
        
        #[cfg(not(target_os = "macos"))]
        {
            // Linux/Windowsの場合は従来通り
            eprintln!("dispatch_async is macOS only");
            return -1;
        }
        
        0
    }
}

fn calculate_nice_max(max_value: f64) -> f64 {
    if max_value <= 0.0 {
        return 100.0;
    }
    
    let magnitude = 10_f64.powf(max_value.log10().floor());
    let normalized = max_value / magnitude;
    
    let nice_normalized = if normalized <= 1.0 {
        1.0
    } else if normalized <= 2.0 {
        2.0
    } else if normalized <= 2.5 {
        2.5
    } else if normalized <= 5.0 {
        5.0
    } else {
        10.0
    };
    
    nice_normalized * magnitude * 1.1
}

impl canvas::Program<Message> for ChartApp {
    type State = ();

    fn draw(
        &self,
        _state: &Self::State,
        renderer: &iced::Renderer,
        _theme: &Theme,
        bounds: Rectangle,
        _cursor: iced::mouse::Cursor,
    ) -> Vec<Geometry> {
        let mut frame = Frame::new(renderer, bounds.size());
        
        if self.data.x_data.is_empty() || self.data.y_data.is_empty() {
            frame.fill_text(iced::widget::canvas::Text {
                content: "No data to display".to_string(),
                position: Point::new(bounds.width / 2.0, bounds.height / 2.0),
                color: Color::BLACK,
                size: Pixels(20.0),
                font: iced::Font::default(),
                horizontal_alignment: iced::alignment::Horizontal::Center,
                vertical_alignment: iced::alignment::Vertical::Center,
                line_height: iced::widget::text::LineHeight::default(),
                shaping: iced::widget::text::Shaping::default(),
            });
            return vec![frame.into_geometry()];
        }
        
        let padding = 60.0;
        let chart_width = bounds.width - padding * 2.0;
        let chart_height = bounds.height - padding * 2.0;
        
        let max_value = self.data.y_data.iter().fold(0.0f64, |a, &b| a.max(b));
        if max_value == 0.0 {
            return vec![frame.into_geometry()];
        }
        
        let y_axis_max = calculate_nice_max(max_value);
        
        match self.data.chart_type.as_str() {
            "line" => {
                self.draw_line_chart(&mut frame, bounds, padding, chart_width, chart_height, y_axis_max);
            },
            "scatter" => {
                self.draw_scatter_chart(&mut frame, bounds, padding, chart_width, chart_height, y_axis_max);
            },
            "area" => {
                self.draw_area_chart(&mut frame, bounds, padding, chart_width, chart_height, y_axis_max);
            },
            "histogram" => {
                self.draw_histogram(&mut frame, bounds, padding, chart_width, chart_height, y_axis_max);
            },
            _ => {
                self.draw_bar_chart(&mut frame, bounds, padding, chart_width, chart_height, y_axis_max);
            }
        }
        
        self.draw_axes(&mut frame, bounds, padding, y_axis_max);
        
        vec![frame.into_geometry()]
    }
}

impl ChartApp {
    fn draw_axes(&self, frame: &mut Frame, bounds: Rectangle, padding: f32, y_max: f64) {
        let axes = Path::new(|p| {
            p.move_to(Point::new(padding, padding));
            p.line_to(Point::new(padding, bounds.height - padding));
            p.line_to(Point::new(bounds.width - padding, bounds.height - padding));
        });
        
        frame.stroke(&axes, Stroke::default().with_width(2.0));
        
        let y_ticks = 5;
        for i in 0..=y_ticks {
            let value = (y_max / y_ticks as f64) * i as f64;
            let y = bounds.height - padding - (i as f32 / y_ticks as f32) * (bounds.height - padding * 2.0);
            
            let tick_path = Path::new(|p| {
                p.move_to(Point::new(padding - 5.0, y));
                p.line_to(Point::new(padding, y));
            });
            frame.stroke(&tick_path, Stroke::default().with_width(1.0));
            
            if i > 0 && i < y_ticks {
                let grid_path = Path::new(|p| {
                    p.move_to(Point::new(padding, y));
                    p.line_to(Point::new(bounds.width - padding, y));
                });
                frame.stroke(&grid_path, Stroke::default().with_width(0.5).with_color(Color::from_rgba(0.5, 0.5, 0.5, 0.3)));
            }
            
            frame.fill_text(iced::widget::canvas::Text {
                content: format!("{:.0}", value),
                position: Point::new(padding - 10.0, y),
                color: Color::BLACK,
                size: Pixels(10.0),
                font: iced::Font::default(),
                horizontal_alignment: iced::alignment::Horizontal::Right,
                vertical_alignment: iced::alignment::Vertical::Center,
                line_height: iced::widget::text::LineHeight::default(),
                shaping: iced::widget::text::Shaping::default(),
            });
        }
    }
    
    fn calculate_label_positions(&self, points: &[(f32, f32)]) -> Vec<f32> {
        let mut label_positions: Vec<f32> = Vec::new();
        let min_spacing: f32 = 25.0;
        
        for (i, &(x, y)) in points.iter().enumerate() {
            let mut label_y: f32 = y - 10.0;
            
            if x < 100.0 {
                label_y = label_y.min(y - 20.0);
            }
            
            for j in 0..i {
                if (points[i].0 - points[j].0).abs() < 100.0 {
                    let prev_label_y: f32 = label_positions[j];
                    let diff: f32 = label_y - prev_label_y;
                    if diff.abs() < min_spacing {
                        if label_y > prev_label_y {
                            label_y = prev_label_y + min_spacing;
                        } else {
                            label_y = prev_label_y - min_spacing;
                        }
                    }
                }
            }
            
            label_positions.push(label_y);
        }
        
        label_positions
    }
    
    fn draw_bar_chart(&self, frame: &mut Frame, bounds: Rectangle, padding: f32, chart_width: f32, chart_height: f32, max_value: f64) {
        let bar_width = chart_width / (self.data.x_data.len() as f32 * 1.5);
        let data_len = self.data.x_data.len().min(self.data.y_data.len());
        
        for i in 0..data_len {
            let x = padding + (i as f32 * bar_width * 1.5) + bar_width * 0.25;
            let height = (self.data.y_data[i] / max_value) * chart_height as f64;
            let y = bounds.height - padding - height as f32;
            
            frame.fill_rectangle(
                Point::new(x, y),
                Size::new(bar_width, height as f32),
                Color::from_rgb(0.2, 0.6, 0.9),
            );
            
            self.draw_x_label(frame, &self.data.x_data[i], x + bar_width / 2.0, bounds.height - padding + 20.0);
            self.draw_value_label_smart(frame, self.data.y_data[i], x + bar_width / 2.0, y);
        }
    }
    
    fn draw_line_chart(&self, frame: &mut Frame, bounds: Rectangle, padding: f32, chart_width: f32, chart_height: f32, max_value: f64) {
        let data_len = self.data.x_data.len().min(self.data.y_data.len());
        if data_len == 0 { return; }
        
        let x_step = chart_width / ((data_len - 1).max(1) as f32);
        
        let mut points = Vec::new();
        for i in 0..data_len {
            let x = padding + (i as f32 * x_step);
            let y = bounds.height - padding - ((self.data.y_data[i] / max_value) * chart_height as f64) as f32;
            points.push((x, y));
        }
        
        let label_positions = self.calculate_label_positions(&points);
        
        let path = Path::new(|p| {
            for (i, &(x, y)) in points.iter().enumerate() {
                if i == 0 {
                    p.move_to(Point::new(x, y));
                } else {
                    p.line_to(Point::new(x, y));
                }
            }
        });
        
        frame.stroke(&path, Stroke::default().with_width(2.0).with_color(Color::from_rgb(0.2, 0.6, 0.9)));
        
        for (i, &(x, y)) in points.iter().enumerate() {
            frame.fill(&Path::circle(Point::new(x, y), 4.0), Color::from_rgb(0.2, 0.6, 0.9));
            self.draw_x_label(frame, &self.data.x_data[i], x, bounds.height - padding + 20.0);
            
            let label_y = label_positions[i];
            if (label_y - (y - 10.0)).abs() > 5.0 {
                let leader = Path::new(|p| {
                    p.move_to(Point::new(x, y - 5.0));
                    p.line_to(Point::new(x, label_y + 7.0));
                });
                frame.stroke(&leader, Stroke::default()
                    .with_width(0.5)
                    .with_color(Color::from_rgba(0.5, 0.5, 0.5, 0.5)));
            }
            
            self.draw_value_label_at(frame, self.data.y_data[i], x, label_y);
        }
    }
    
    fn draw_scatter_chart(&self, frame: &mut Frame, bounds: Rectangle, padding: f32, chart_width: f32, chart_height: f32, max_value: f64) {
        let data_len = self.data.x_data.len().min(self.data.y_data.len());
        
        let x_numeric: Vec<f64> = self.data.x_data.iter()
            .filter_map(|s| s.parse().ok())
            .collect();
        
        let use_numeric_x = x_numeric.len() == data_len;
        let max_x = if use_numeric_x {
            x_numeric.iter().fold(0.0f64, |a, &b| a.max(b))
        } else {
            data_len as f64
        };
        
        let mut points = Vec::new();
        for i in 0..data_len {
            let x = if use_numeric_x {
                padding + (x_numeric[i] / max_x) as f32 * chart_width
            } else {
                padding + (i as f32 / (data_len - 1).max(1) as f32) * chart_width
            };
            let y = bounds.height - padding - ((self.data.y_data[i] / max_value) * chart_height as f64) as f32;
            points.push((x, y));
        }
        
        let label_positions = self.calculate_label_positions(&points);
        
        for (i, &(x, y)) in points.iter().enumerate() {
            frame.fill(&Path::circle(Point::new(x, y), 5.0), Color::from_rgb(0.2, 0.6, 0.9));
            
            if i % ((data_len / 5).max(1)) == 0 || data_len <= 5 {
                self.draw_x_label(frame, &self.data.x_data[i], x, bounds.height - padding + 20.0);
            }
            
            let label_y = label_positions[i];
            if (label_y - (y - 10.0)).abs() > 5.0 {
                let leader = Path::new(|p| {
                    p.move_to(Point::new(x, y - 5.0));
                    p.line_to(Point::new(x, label_y + 7.0));
                });
                frame.stroke(&leader, Stroke::default()
                    .with_width(0.5)
                    .with_color(Color::from_rgba(0.5, 0.5, 0.5, 0.5)));
            }
            
            self.draw_value_label_at(frame, self.data.y_data[i], x, label_y);
        }
    }
    
    fn draw_area_chart(&self, frame: &mut Frame, bounds: Rectangle, padding: f32, chart_width: f32, chart_height: f32, max_value: f64) {
        let data_len = self.data.x_data.len().min(self.data.y_data.len());
        if data_len == 0 { return; }
        
        let x_step = chart_width / ((data_len - 1).max(1) as f32);
        
        let mut points = Vec::new();
        for i in 0..data_len {
            let x = padding + (i as f32 * x_step);
            let y = bounds.height - padding - ((self.data.y_data[i] / max_value) * chart_height as f64) as f32;
            points.push((x, y));
        }
        
        let label_positions = self.calculate_label_positions(&points);
        
        let path = Path::new(|p| {
            p.move_to(Point::new(padding, bounds.height - padding));
            for &(x, y) in &points {
                p.line_to(Point::new(x, y));
            }
            p.line_to(Point::new(padding + ((data_len - 1) as f32 * x_step), bounds.height - padding));
            p.close();
        });
        
        frame.fill(&path, Color::from_rgba(0.2, 0.6, 0.9, 0.3));
        
        let outline_path = Path::new(|p| {
            for (i, &(x, y)) in points.iter().enumerate() {
                if i == 0 {
                    p.move_to(Point::new(x, y));
                } else {
                    p.line_to(Point::new(x, y));
                }
            }
        });
        
        frame.stroke(&outline_path, Stroke::default().with_width(2.0).with_color(Color::from_rgb(0.2, 0.6, 0.9)));
        
        for (i, &(x, y)) in points.iter().enumerate() {
            frame.fill(&Path::circle(Point::new(x, y), 4.0), Color::from_rgb(0.2, 0.6, 0.9));
            self.draw_x_label(frame, &self.data.x_data[i], x, bounds.height - padding + 20.0);
            
            let label_y = label_positions[i];
            if (label_y - (y - 10.0)).abs() > 5.0 {
                let leader = Path::new(|p| {
                    p.move_to(Point::new(x, y - 5.0));
                    p.line_to(Point::new(x, label_y + 7.0));
                });
                frame.stroke(&leader, Stroke::default()
                    .with_width(0.5)
                    .with_color(Color::from_rgba(0.5, 0.5, 0.5, 0.5)));
            }
            
            self.draw_value_label_at(frame, self.data.y_data[i], x, label_y);
        }
    }
    
    fn draw_histogram(&self, frame: &mut Frame, bounds: Rectangle, padding: f32, chart_width: f32, chart_height: f32, max_value: f64) {
        let bar_width = chart_width / (self.data.y_data.len() as f32);
        
        for i in 0..self.data.y_data.len() {
            let x = padding + (i as f32 * bar_width);
            let height = (self.data.y_data[i] / max_value) * chart_height as f64;
            let y = bounds.height - padding - height as f32;
            
            frame.fill_rectangle(
                Point::new(x, y),
                Size::new(bar_width - 2.0, height as f32),
                Color::from_rgb(0.2, 0.6, 0.9),
            );
        }
    }
    
    fn draw_x_label(&self, frame: &mut Frame, label: &str, x: f32, y: f32) {
        frame.fill_text(iced::widget::canvas::Text {
            content: label.to_string(),
            position: Point::new(x, y),
            color: Color::BLACK,
            size: Pixels(12.0),
            font: iced::Font::default(),
            horizontal_alignment: iced::alignment::Horizontal::Center,
            vertical_alignment: iced::alignment::Vertical::Top,
            line_height: iced::widget::text::LineHeight::default(),
            shaping: iced::widget::text::Shaping::default(),
        });
    }
    
    fn draw_value_label_smart(&self, frame: &mut Frame, value: f64, x: f32, y: f32) {
        let adjusted_y = (y - 15.0).max(80.0);
        self.draw_value_label_at(frame, value, x, adjusted_y);
    }
    
    fn draw_value_label_at(&self, frame: &mut Frame, value: f64, x: f32, y: f32) {
        let adjusted_x = if x < 90.0 {
            x + 30.0
        } else {
            x
        };
        
        let text_content = format!("{:.0}", value);
        let text_width = text_content.len() as f32 * 6.0 + 8.0;
        let text_height = 14.0;
        
        frame.fill_rectangle(
            Point::new(adjusted_x - text_width / 2.0, y - text_height),
            Size::new(text_width, text_height),
            Color::from_rgba(1.0, 1.0, 1.0, 0.95),
        );
        
        frame.stroke(
            &Path::rectangle(
                Point::new(adjusted_x - text_width / 2.0, y - text_height),
                Size::new(text_width, text_height)
            ),
            Stroke::default()
                .with_width(0.5)
                .with_color(Color::from_rgba(0.8, 0.8, 0.8, 0.5))
        );
        
        frame.fill_text(iced::widget::canvas::Text {
            content: text_content,
            position: Point::new(adjusted_x, y),
            color: Color::BLACK,
            size: Pixels(10.0),
            font: iced::Font::default(),
            horizontal_alignment: iced::alignment::Horizontal::Center,
            vertical_alignment: iced::alignment::Vertical::Bottom,
            line_height: iced::widget::text::LineHeight::default(),
            shaping: iced::widget::text::Shaping::default(),
        });
    }
}