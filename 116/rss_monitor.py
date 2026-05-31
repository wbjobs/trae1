import feedparser
import httpx
import re
import time
from datetime import datetime, timedelta
from typing import List, Dict, Optional, Tuple
from loguru import logger
from urllib.parse import urlparse
from config import RSSFeedConfig


class RSSVideoEntry:
    def __init__(self, title: str, url: str, published: datetime, author: str = "", thumbnail: str = ""):
        self.title = title
        self.url = url
        self.published = published
        self.author = author
        self.thumbnail = thumbnail
        self.video_id = self._extract_video_id(url)

    def _extract_video_id(self, url: str) -> str:
        parsed = urlparse(url)
        if "bilibili.com" in parsed.netloc:
            match = re.search(r'BV[a-zA-Z0-9]+', url)
            if match:
                return match.group(0)
        elif "youtube.com" in parsed.netloc or "youtu.be" in parsed.netloc:
            match = re.search(r'[?&]v=([^&]+)', url)
            if match:
                return match.group(1)
            match = re.search(r'youtu\.be/([^?]+)', url)
            if match:
                return match.group(1)
        return re.sub(r'[^a-zA-Z0-9]', '', url)[:32]

    def to_dict(self) -> Dict:
        return {
            "title": self.title,
            "url": self.url,
            "video_id": self.video_id,
            "published": self.published.isoformat(),
            "author": self.author,
            "thumbnail": self.thumbnail
        }


class RSSMonitor:
    def __init__(self):
        self.feeds: List[RSSFeedConfig] = []
        self.processed_entries: Dict[str, datetime] = {}
        self.max_processed_history = 10000

    def add_feed(self, config: RSSFeedConfig) -> None:
        self.feeds.append(config)
        logger.info(f"已添加RSS订阅: {config.name} ({config.url})")

    def remove_feed(self, name: str) -> bool:
        for i, feed in enumerate(self.feeds):
            if feed.name == name:
                self.feeds.pop(i)
                logger.info(f"已移除RSS订阅: {name}")
                return True
        return False

    def get_feeds(self) -> List[Dict]:
        return [{
            "name": feed.name,
            "url": feed.url,
            "platform": feed.platform,
            "enabled": feed.enabled,
            "check_interval": feed.check_interval,
            "last_checked": feed.last_checked.isoformat() if feed.last_checked else None
        } for feed in self.feeds]

    def parse_entry_datetime(self, entry: feedparser.FeedParserDict) -> datetime:
        if hasattr(entry, 'published_parsed') and entry.published_parsed:
            return datetime.fromtimestamp(time.mktime(entry.published_parsed))
        elif hasattr(entry, 'updated_parsed') and entry.updated_parsed:
            return datetime.fromtimestamp(time.mktime(entry.updated_parsed))
        return datetime.now()

    def extract_video_url(self, entry: feedparser.FeedParserDict) -> str:
        if hasattr(entry, 'link'):
            return entry.link
        if hasattr(entry, 'id'):
            return entry.id
        return ""

    def extract_author(self, entry: feedparser.FeedParserDict) -> str:
        if hasattr(entry, 'author'):
            return entry.author
        if hasattr(entry, 'author_detail') and entry.author_detail:
            return entry.author_detail.get('name', '')
        return ""

    def extract_thumbnail(self, entry: feedparser.FeedParserDict) -> str:
        if hasattr(entry, 'media_thumbnail') and entry.media_thumbnail:
            return entry.media_thumbnail[0].get('url', '')
        if hasattr(entry, 'links'):
            for link in entry.links:
                if link.get('rel') == 'enclosure' and 'image' in link.get('type', ''):
                    return link.get('url', '')
        return ""

    def parse_feed(self, feed_config: RSSFeedConfig) -> List[RSSVideoEntry]:
        entries = []
        try:
            logger.info(f"解析RSS源: {feed_config.name}")
            
            parsed = feedparser.parse(feed_config.url)
            
            if parsed.bozo != 0:
                logger.warning(f"RSS解析警告 {feed_config.name}: {parsed.bozo_exception}")

            for entry in parsed.entries[:feed_config.max_entries_per_check]:
                try:
                    video_entry = RSSVideoEntry(
                        title=entry.get('title', ''),
                        url=self.extract_video_url(entry),
                        published=self.parse_entry_datetime(entry),
                        author=self.extract_author(entry),
                        thumbnail=self.extract_thumbnail(entry)
                    )
                    
                    if video_entry.url:
                        entries.append(video_entry)
                        
                except Exception as e:
                    logger.error(f"解析RSS条目失败: {e}")
                    continue

            feed_config.last_checked = datetime.now()
            logger.info(f"从 {feed_config.name} 获取到 {len(entries)} 条视频")
            
        except Exception as e:
            logger.error(f"解析RSS源失败 {feed_config.name}: {e}")
            
        return entries

    def check_new_entries(self, feed_config: RSSFeedConfig, 
                          since: Optional[datetime] = None) -> List[RSSVideoEntry]:
        if not feed_config.enabled:
            return []

        all_entries = self.parse_feed(feed_config)
        new_entries = []

        for entry in all_entries:
            entry_key = f"{feed_config.name}:{entry.video_id}"
            
            if entry_key in self.processed_entries:
                continue

            if since and entry.published < since:
                continue

            new_entries.append(entry)
            self.processed_entries[entry_key] = datetime.now()

        while len(self.processed_entries) > self.max_processed_history:
            oldest_key = min(self.processed_entries.keys(), 
                           key=lambda k: self.processed_entries[k])
            del self.processed_entries[oldest_key]

        logger.info(f"{feed_config.name}: 发现 {len(new_entries)} 条新视频")
        return new_entries

    def check_all_feeds(self) -> Dict[str, List[RSSVideoEntry]]:
        results = {}
        for feed in self.feeds:
            if feed.enabled:
                new_entries = self.check_new_entries(feed)
                if new_entries:
                    results[feed.name] = new_entries
        return results

    def add_bilibili_feed(self, user_id: str, feed_name: Optional[str] = None) -> None:
        feed_url = f"https://rsshub.app/bilibili/user/video/{user_id}"
        config = RSSFeedConfig(
            name=feed_name or f"bilibili_{user_id}",
            url=feed_url,
            platform="bilibili",
            check_interval=1800
        )
        self.add_feed(config)

    def add_youtube_feed(self, channel_id: str, feed_name: Optional[str] = None) -> None:
        feed_url = f"https://www.youtube.com/feeds/videos.xml?channel_id={channel_id}"
        config = RSSFeedConfig(
            name=feed_name or f"youtube_{channel_id}",
            url=feed_url,
            platform="youtube",
            check_interval=1800
        )
        self.add_feed(config)


async def test_rss_monitor():
    monitor = RSSMonitor()
    
    monitor.add_feed(RSSFeedConfig(
        name="test_bilibili",
        url="https://rsshub.app/bilibili/user/video/2",
        platform="bilibili",
        check_interval=300
    ))
    
    entries = monitor.check_all_feeds()
    
    for feed_name, video_entries in entries.items():
        print(f"\n=== {feed_name} ===")
        for entry in video_entries[:5]:
            print(f"标题: {entry.title}")
            print(f"URL: {entry.url}")
            print(f"发布时间: {entry.published}")
            print(f"作者: {entry.author}")
            print("-" * 50)


if __name__ == "__main__":
    import asyncio
    asyncio.run(test_rss_monitor())
