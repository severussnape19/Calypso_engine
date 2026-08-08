#[macro_export]
macro_rules! log {
    (INFO, $($arg:tt)*) => {
        eprintln!(
            "\x1b[32m[LOG]\x1b[0m [{}:{}] {}",
            file!(),
            line!(),
            format_args!($($arg)*)
        )
    };
}

#[macro_export]
macro_rules! warn {
    (WARN, $($arg:tt)*) => {
        eprintln!(
            "\x1b[38;5;208m[WARN]\x1b[0m [{}:{}] {}",
            file!(),
            line!(),
            format_args!($($arg)*)
        )
    };
}
