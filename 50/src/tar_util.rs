use std::fs::File;
use std::path::{Path, PathBuf};
use tar::{Builder, Header, Archive};

pub fn pack_files_to_tar<P: AsRef<Path>>(
    files: &[PathBuf],
    output_path: P,
) -> anyhow::Result<u64> {
    let output_file = File::create(&output_path)?;
    let mut builder = Builder::new(output_file);

    for file_path in files {
        let path = Path::new(file_path);
        if !path.exists() {
            anyhow::bail!("File not found: {:?}", path);
        }

        let metadata = std::fs::metadata(path)?;
        let file_size = metadata.len();

        let mut header = Header::new_gnu();
        header.set_size(file_size);
        header.set_mode(0o644);
        header.set_cksum();

        let file_name = path
            .file_name()
            .ok_or_else(|| anyhow::anyhow!("Invalid file path: {:?}", path))?;

        let mut file = File::open(path)?;
        builder.append_data(&mut header, file_name, &mut file)?;
    }

    builder.finish()?;
    let output_file = builder.into_inner()?;
    let tar_size = output_file.metadata()?.len();

    Ok(tar_size)
}

pub fn unpack_tar_to_dir<P: AsRef<Path>, Q: AsRef<Path>>(
    tar_path: P,
    output_dir: Q,
) -> anyhow::Result<Vec<PathBuf>> {
    let output_dir = output_dir.as_ref();
    std::fs::create_dir_all(output_dir)?;

    let tar_file = File::open(&tar_path)?;
    let mut archive = Archive::new(tar_file);

    let mut extracted_files = Vec::new();

    for entry in archive.entries()? {
        let mut entry = entry?;
        let path = entry.path()?.to_path_buf();
        let output_path = output_dir.join(&path);

        if let Some(parent) = output_path.parent() {
            std::fs::create_dir_all(parent)?;
        }

        entry.unpack(&output_path)?;
        extracted_files.push(output_path);
    }

    Ok(extracted_files)
}
