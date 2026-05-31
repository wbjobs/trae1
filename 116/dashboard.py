from datetime import datetime, timedelta
from typing import Dict, List, Optional, Any
from collections import defaultdict, Counter
from loguru import logger
from config import InfringementRecord


class DashboardStats:
    def __init__(self):
        self.infringement_records: List[InfringementRecord] = []
        self.monitor_stats: Dict[str, Any] = defaultdict(lambda: {
            "total_scans": 0,
            "videos_checked": 0,
            "infringements_found": 0
        })

    def add_infringement_record(self, record: InfringementRecord) -> None:
        self.infringement_records.append(record)

    def update_monitor_stats(self, monitor_name: str, videos_checked: int,
                             infringements_found: int) -> None:
        self.monitor_stats[monitor_name]["total_scans"] += 1
        self.monitor_stats[monitor_name]["videos_checked"] += videos_checked
        self.monitor_stats[monitor_name]["infringements_found"] += infringements_found

    def get_overview_stats(self) -> Dict[str, Any]:
        total_records = len(self.infringement_records)
        today = datetime.now().date()
        
        today_records = [
            r for r in self.infringement_records
            if r.detected_at.date() == today
        ]
        
        status_counts = Counter(r.status for r in self.infringement_records)
        
        return {
            "total_infringements": total_records,
            "today_infringements": len(today_records),
            "status_distribution": dict(status_counts),
            "active_monitors": len(self.monitor_stats),
            "total_scans": sum(s["total_scans"] for s in self.monitor_stats.values())
        }

    def get_infringements_by_platform(self, 
                                       start_date: Optional[datetime] = None,
                                       end_date: Optional[datetime] = None) -> Dict[str, int]:
        records = self._filter_by_date(self.infringement_records, start_date, end_date)
        platform_counts = Counter(r.platform for r in records)
        return dict(platform_counts)

    def get_infringements_by_infringer(self, 
                                        start_date: Optional[datetime] = None,
                                        end_date: Optional[datetime] = None,
                                        top_n: int = 10) -> List[Dict]:
        records = self._filter_by_date(self.infringement_records, start_date, end_date)
        infringer_counts = Counter(r.infringer for r in records if r.infringer)
        
        result = []
        for infringer, count in infringer_counts.most_common(top_n):
            avg_similarity = self._get_avg_similarity(records, infringer)
            result.append({
                "infringer": infringer,
                "count": count,
                "avg_similarity": avg_similarity
            })
        
        return result

    def get_infringements_by_similarity(self,
                                         start_date: Optional[datetime] = None,
                                         end_date: Optional[datetime] = None,
                                         buckets: int = 10) -> Dict[str, int]:
        records = self._filter_by_date(self.infringement_records, start_date, end_date)
        
        bucket_size = 1.0 / buckets
        distribution = defaultdict(int)
        
        for record in records:
            bucket = int(record.similarity / bucket_size) * bucket_size
            bucket_key = f"{bucket:.1f}-{bucket + bucket_size:.1f}"
            distribution[bucket_key] += 1
        
        return dict(sorted(distribution.items()))

    def get_infringements_by_time(self,
                                   start_date: Optional[datetime] = None,
                                   end_date: Optional[datetime] = None,
                                   group_by: str = "day") -> Dict[str, int]:
        records = self._filter_by_date(self.infringement_records, start_date, end_date)
        
        distribution = defaultdict(int)
        
        for record in records:
            if group_by == "hour":
                key = record.detected_at.strftime("%Y-%m-%d %H:00")
            elif group_by == "day":
                key = record.detected_at.strftime("%Y-%m-%d")
            elif group_by == "week":
                key = record.detected_at.strftime("%Y-%W")
            elif group_by == "month":
                key = record.detected_at.strftime("%Y-%m")
            else:
                key = record.detected_at.strftime("%Y-%m-%d")
            
            distribution[key] += 1
        
        return dict(sorted(distribution.items()))

    def get_top_original_videos(self,
                                 start_date: Optional[datetime] = None,
                                 end_date: Optional[datetime] = None,
                                 top_n: int = 10) -> List[Dict]:
        records = self._filter_by_date(self.infringement_records, start_date, end_date)
        video_counts = Counter(r.original_video_id for r in records if r.original_video_id)
        
        result = []
        for video_id, count in video_counts.most_common(top_n):
            video_records = [r for r in records if r.original_video_id == video_id]
            if video_records:
                result.append({
                    "video_id": video_id,
                    "video_title": video_records[0].original_video_title,
                    "infringement_count": count,
                    "avg_similarity": self._get_avg_similarity(video_records)
                })
        
        return result

    def get_monitor_performance(self) -> List[Dict]:
        result = []
        for monitor_name, stats in self.monitor_stats.items():
            result.append({
                "monitor_name": monitor_name,
                "total_scans": stats["total_scans"],
                "videos_checked": stats["videos_checked"],
                "infringements_found": stats["infringements_found"],
                "detection_rate": (
                    stats["infringements_found"] / stats["videos_checked"]
                    if stats["videos_checked"] > 0 else 0
                )
            })
        return sorted(result, key=lambda x: x["infringements_found"], reverse=True)

    def get_trend_data(self, days: int = 7) -> Dict[str, Any]:
        end_date = datetime.now()
        start_date = end_date - timedelta(days=days)
        
        daily_stats = self.get_infringements_by_time(
            start_date=start_date,
            end_date=end_date,
            group_by="day"
        )
        
        platform_stats = self.get_infringements_by_platform(
            start_date=start_date,
            end_date=end_date
        )
        
        similarity_stats = self.get_infringements_by_similarity(
            start_date=start_date,
            end_date=end_date
        )
        
        return {
            "period_days": days,
            "daily_infringements": daily_stats,
            "platform_distribution": platform_stats,
            "similarity_distribution": similarity_stats
        }

    def get_detailed_records(self,
                              status: Optional[str] = None,
                              platform: Optional[str] = None,
                              start_date: Optional[datetime] = None,
                              end_date: Optional[datetime] = None,
                              limit: int = 100) -> List[Dict]:
        records = self._filter_by_date(self.infringement_records, start_date, end_date)
        
        if status:
            records = [r for r in records if r.status == status]
        if platform:
            records = [r for r in records if r.platform == platform]
        
        records.sort(key=lambda r: r.detected_at, reverse=True)
        
        return [r.to_dict() for r in records[:limit]]

    def _filter_by_date(self, records: List[InfringementRecord],
                         start_date: Optional[datetime] = None,
                         end_date: Optional[datetime] = None) -> List[InfringementRecord]:
        result = records
        
        if start_date:
            result = [r for r in result if r.detected_at >= start_date]
        if end_date:
            result = [r for r in result if r.detected_at <= end_date]
        
        return result

    def _get_avg_similarity(self, records: List[InfringementRecord],
                            infringer: Optional[str] = None) -> float:
        filtered = records
        if infringer:
            filtered = [r for r in records if r.infringer == infringer]
        
        if not filtered:
            return 0.0
        
        return sum(r.similarity for r in filtered) / len(filtered)
