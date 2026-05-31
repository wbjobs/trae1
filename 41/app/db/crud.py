import json
from datetime import datetime
from sqlalchemy.orm import Session
from app.db.models import ScanResult


class ScanResultCRUD:
    def __init__(self, db: Session):
        self.db = db

    def get_by_md5(self, file_md5: str) -> ScanResult | None:
        return self.db.query(ScanResult).filter(ScanResult.file_md5 == file_md5).first()

    def create(
        self,
        file_md5: str,
        file_name: str | None,
        file_size: int | None,
        matched_rules: list,
        total_matches: int,
        highest_severity: str,
    ) -> ScanResult:
        result = ScanResult(
            file_md5=file_md5,
            file_name=file_name,
            file_size=file_size,
            matched_rules=json.dumps(matched_rules) if matched_rules else "[]",
            scan_time=datetime.utcnow(),
            total_matches=total_matches,
            highest_severity=highest_severity,
        )
        self.db.add(result)
        self.db.commit()
        self.db.refresh(result)
        return result

    def update(
        self,
        file_md5: str,
        matched_rules: list,
        total_matches: int,
        highest_severity: str,
    ) -> ScanResult | None:
        result = self.get_by_md5(file_md5)
        if result:
            result.matched_rules = json.dumps(matched_rules) if matched_rules else "[]"
            result.scan_time = datetime.utcnow()
            result.total_matches = total_matches
            result.highest_severity = highest_severity
            self.db.commit()
            self.db.refresh(result)
        return result

    def delete(self, file_md5: str) -> bool:
        result = self.get_by_md5(file_md5)
        if result:
            self.db.delete(result)
            self.db.commit()
            return True
        return False

    def list_results(
        self,
        skip: int = 0,
        limit: int = 100,
        severity: str | None = None,
    ) -> list[ScanResult]:
        query = self.db.query(ScanResult)
        if severity:
            query = query.filter(ScanResult.highest_severity == severity)
        return query.order_by(ScanResult.scan_time.desc()).offset(skip).limit(limit).all()

    def count(self, severity: str | None = None) -> int:
        query = self.db.query(ScanResult)
        if severity:
            query = query.filter(ScanResult.highest_severity == severity)
        return query.count()
