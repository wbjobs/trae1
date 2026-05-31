from setuptools import setup, find_packages

with open("README.md", "r", encoding="utf-8") as fh:
    long_description = fh.read()

setup(
    name="mylvmbackup",
    version="0.1.0",
    author="mylvmbackup",
    description="MySQL 热备份恢复 CLI 工具，基于 LVM 精简快照 + mydumper + S3",
    long_description=long_description,
    long_description_content_type="text/markdown",
    packages=find_packages(exclude=("tests", "tests.*")),
    python_requires=">=3.8",
    install_requires=[
        "click>=8.0",
        "PyYAML>=6.0",
        "boto3>=1.26",
        "mysql-connector-python>=8.0",
        "tenacity>=8.0",
    ],
    entry_points={
        "console_scripts": [
            "mylvmbackup=mylvmbackup.cli:cli",
        ],
    },
    classifiers=[
        "Programming Language :: Python :: 3",
        "Operating System :: POSIX :: Linux",
        "Topic :: Database",
    ],
)
