use iced::{
    Application, Command, Element, Settings, Theme, Length,
    widget::{canvas, column, container, text},
};
use iced::widget::canvas::{Frame, Geometry, Path, Stroke};
use iced::{Color, Point, Rectangle, Size, Pixels};
use std::env;
use std::fs;

#[derive(Default)]
struct ChartData {
    title: String,
    x_data: Vec<String>,
    y_data: Vec<f64>,
    chart_type: String,
}

fn main() -> iced::Result {
    let args: Vec<String> = env::args().collect();
    
    // コマンドライン引数から読み取り
    // args[0]: プログラム名
    // args[1]: データファイルパス
    // args[2]: チャートタイプ（オプション）
    let data = if args.len() > 1 {
        let chart_type = if args.len() > 2 {
            args[2].clone()
        } else {
            "bar".to_string()
        };
        load_chart_data(&args[1], chart_type)
    } else {
        ChartData {
            title: "Sample Chart".to_string(),
            x_data: vec!["A".to_string(), "B".to_string(), "C".to_string()],
            y_data: vec![30.0, 50.0, 80.0],
            chart_type: "bar".to_string(),
        }
    };
    
    ChartApp::run(Settings {
        window: iced::window::Settings {
            size: Size::new(800.0, 600.0),
            position: iced::window::Position::Centered,
            ..Default::default()
        },
        flags: data,
        ..Default::default()
    })
}

fn load_chart_data(file_path: &str, chart_type: String) -> ChartData {
    match fs::read_to_string(file_path) {
        Ok(content) => {
            let lines: Vec<&str> = content.lines().collect();
            if lines.len() >= 3 {
                let title = lines[0].to_string();
                let x_data: Vec<String> = lines[1].split(',').map(|s| s.trim().to_string()).collect();
                let y_data: Vec<f64> = lines[2].split(',')
                    .filter_map(|s| s.trim().parse().ok())
                    .collect();
                
                ChartData { title, x_data, y_data, chart_type }
            } else {
                ChartData {
                    title: "Error: Invalid data format".to_string(),
                    x_data: vec![],
                    y_data: vec![],
                    chart_type,
                }
            }
        }
        Err(_) => ChartData {
            title: "Error loading data".to_string(),
            x_data: vec![],
            y_data: vec![],
            chart_type,
        }
    }
}

struct ChartApp {
    data: ChartData,
}

#[derive(Debug, Clone)]
enum Message {}

impl Application for ChartApp {
    type Message = Message;
    type Theme = Theme;
    type Executor = iced::executor::Default;
    type Flags = ChartData;

    fn new(flags: Self::Flags) -> (Self, Command<Message>) {
        (ChartApp { data: flags }, Command::none())
    }

    fn title(&self) -> String {
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

    fn update(&mut self, _message: Message) -> Command<Message> {
        Command::none()
    }

    fn view(&self) -> Element<Message> {
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
}

impl<Message> canvas::Program<Message> for ChartApp {
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
        
        // チャートタイプに応じて描画
        match self.data.chart_type.as_str() {
            "line" => {
                self.draw_line_chart(&mut frame, bounds, padding, chart_width, chart_height, max_value);
            },
            "scatter" => {
                self.draw_scatter_chart(&mut frame, bounds, padding, chart_width, chart_height, max_value);
            },
            "area" => {
                self.draw_area_chart(&mut frame, bounds, padding, chart_width, chart_height, max_value);
            },
            "histogram" => {
                self.draw_histogram(&mut frame, bounds, padding, chart_width, chart_height, max_value);
            },
            _ => {
                self.draw_bar_chart(&mut frame, bounds, padding, chart_width, chart_height, max_value);
            }
        }
        
        // 軸を描画
        let axes = Path::new(|p| {
            p.move_to(Point::new(padding, padding));
            p.line_to(Point::new(padding, bounds.height - padding));
            p.line_to(Point::new(bounds.width - padding, bounds.height - padding));
        });
        
        frame.stroke(&axes, Stroke::default().with_width(2.0));
        
        vec![frame.into_geometry()]
    }
}

impl ChartApp {
    fn draw_bar_chart(&self, frame: &mut Frame, bounds: Rectangle, padding: f32, chart_width: f32, chart_height: f32, max_value: f64) {
        let bar_width = chart_width / (self.data.x_data.len() as f32 * 1.5);
        let data_len = self.data.x_data.len().min(self.data.y_data.len());
        
        for i in 0..data_len {
            let x = padding + (i as f32 * bar_width * 1.5);
            let height = (self.data.y_data[i] / max_value) * (chart_height as f64) * 0.8;
            let y = bounds.height - padding - height as f32;
            
            frame.fill_rectangle(
                Point::new(x, y),
                Size::new(bar_width, height as f32),
                Color::from_rgb(0.2, 0.6, 0.9),
            );
            
            self.draw_x_label(frame, &self.data.x_data[i], x + bar_width / 2.0, bounds.height - padding + 20.0);
            self.draw_value_label(frame, self.data.y_data[i], x + bar_width / 2.0, y - 5.0);
        }
    }
    
    fn draw_line_chart(&self, frame: &mut Frame, bounds: Rectangle, padding: f32, chart_width: f32, chart_height: f32, max_value: f64) {
        let data_len = self.data.x_data.len().min(self.data.y_data.len());
        if data_len == 0 { return; }
        
        let x_step = chart_width / ((data_len - 1).max(1) as f32);
        
        let path = Path::new(|p| {
            for i in 0..data_len {
                let x = padding + (i as f32 * x_step);
                let y = bounds.height - padding - ((self.data.y_data[i] / max_value) * (chart_height as f64) * 0.8) as f32;
                
                if i == 0 {
                    p.move_to(Point::new(x, y));
                } else {
                    p.line_to(Point::new(x, y));
                }
            }
        });
        
        frame.stroke(&path, Stroke::default().with_width(2.0).with_color(Color::from_rgb(0.2, 0.6, 0.9)));
        
        for i in 0..data_len {
            let x = padding + (i as f32 * x_step);
            let y = bounds.height - padding - ((self.data.y_data[i] / max_value) * (chart_height as f64) * 0.8) as f32;
            
            frame.fill(&Path::circle(Point::new(x, y), 4.0), Color::from_rgb(0.2, 0.6, 0.9));
            
            self.draw_x_label(frame, &self.data.x_data[i], x, bounds.height - padding + 20.0);
            self.draw_value_label(frame, self.data.y_data[i], x, y - 10.0);
        }
    }
    
    fn draw_scatter_chart(&self, frame: &mut Frame, bounds: Rectangle, padding: f32, chart_width: f32, chart_height: f32, max_value: f64) {
        let data_len = self.data.x_data.len().min(self.data.y_data.len());
        
        // X軸データが数値の場合の処理
        let x_numeric: Vec<f64> = self.data.x_data.iter()
            .filter_map(|s| s.parse().ok())
            .collect();
        
        let use_numeric_x = x_numeric.len() == data_len;
        let max_x = if use_numeric_x {
            x_numeric.iter().fold(0.0f64, |a, &b| a.max(b))
        } else {
            data_len as f64
        };
        
        for i in 0..data_len {
            let x = if use_numeric_x {
                padding + (x_numeric[i] / max_x) as f32 * chart_width
            } else {
                padding + (i as f32 / (data_len - 1).max(1) as f32) * chart_width
            };
            let y = bounds.height - padding - ((self.data.y_data[i] / max_value) * (chart_height as f64) * 0.8) as f32;
            
            frame.fill(&Path::circle(Point::new(x, y), 5.0), Color::from_rgb(0.2, 0.6, 0.9));
            
            if i % ((data_len / 5).max(1)) == 0 || data_len <= 5 {
                self.draw_x_label(frame, &self.data.x_data[i], x, bounds.height - padding + 20.0);
            }
        }
    }
    
    fn draw_area_chart(&self, frame: &mut Frame, bounds: Rectangle, padding: f32, chart_width: f32, chart_height: f32, max_value: f64) {
        let data_len = self.data.x_data.len().min(self.data.y_data.len());
        if data_len == 0 { return; }
        
        let x_step = chart_width / ((data_len - 1).max(1) as f32);
        
        let path = Path::new(|p| {
            p.move_to(Point::new(padding, bounds.height - padding));
            
            for i in 0..data_len {
                let x = padding + (i as f32 * x_step);
                let y = bounds.height - padding - ((self.data.y_data[i] / max_value) * (chart_height as f64) * 0.8) as f32;
                p.line_to(Point::new(x, y));
            }
            
            p.line_to(Point::new(padding + ((data_len - 1) as f32 * x_step), bounds.height - padding));
            p.close();
        });
        
        frame.fill(&path, Color::from_rgba(0.2, 0.6, 0.9, 0.3));
        
        for i in 0..data_len {
            let x = padding + (i as f32 * x_step);
            self.draw_x_label(frame, &self.data.x_data[i], x, bounds.height - padding + 20.0);
        }
    }
    
    fn draw_histogram(&self, frame: &mut Frame, bounds: Rectangle, padding: f32, chart_width: f32, chart_height: f32, max_value: f64) {
        let bar_width = chart_width / (self.data.y_data.len() as f32);
        
        for i in 0..self.data.y_data.len() {
            let x = padding + (i as f32 * bar_width);
            let height = (self.data.y_data[i] / max_value) * (chart_height as f64) * 0.8;
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
    
    fn draw_value_label(&self, frame: &mut Frame, value: f64, x: f32, y: f32) {
        frame.fill_text(iced::widget::canvas::Text {
            content: format!("{:.1}", value),
            position: Point::new(x, y),
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