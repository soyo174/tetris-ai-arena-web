use std::io::{self, BufRead, Write};

fn request_id(line: &str) -> Option<&str> {
    let (_, field) = line.split_once("\"request_id\"")?;
    let value = field.trim_start().strip_prefix(':')?.trim_start();
    let (id, _) = value.strip_prefix('"')?.split_once('"')?;
    (!id.is_empty() && id.bytes().all(|byte| byte.is_ascii_digit())).then_some(id)
}

fn main() -> io::Result<()> {
    let mut output = io::stdout().lock();
    for line in io::stdin().lock().lines() {
        let line = line?;
        if let Some(id) = request_id(&line) {
            writeln!(
                output,
                "{{\"type\":\"PLAY\",\"request_id\":\"{}\",\"commands\":[\"HARD_DROP\"]}}",
                id
            )?;
            output.flush()?;
        }
    }
    Ok(())
}
