import os
import json
import time
import signal
import sys
import threading
import random
import argparse
from collections import deque
from flask import Flask, request, jsonify, send_from_directory
from flask_cors import CORS

from database import Database
from magnet_parser import MagnetParser, DHTSeederEstimator
from fingerprint import (
    ContentFingerprint, CopyrightDatabase, DMCAGenerator, PerceptualHash
)


app = Flask(__name__, static_folder='../../frontend/dist', static_url_path='')
CORS(app)

DB_PATH = os.path.join(os.path.dirname(__file__), '..', 'data', 'bt_monitor.db')
FINGERPRINT_DB_PATH = os.path.join(
    os.path.dirname(__file__), '..', 'data', 'copyright_fingerprints.json'
)

db = Database(DB_PATH)
fingerprint_engine = ContentFingerprint()
copyright_db = CopyrightDatabase(FINGERPRINT_DB_PATH)

CRAWL_INTERVAL = int(os.environ.get('CRAWL_INTERVAL', '60'))

_announce_count: Dict[str, int] = {}
_announce_lock = threading.Lock()

ADMIN_TOKEN = os.environ.get('ADMIN_TOKEN', 'admin-secret-change-me')


def _is_admin_request() -> bool:
    token = request.headers.get('X-Admin-Token', '')
    return token == ADMIN_TOKEN


def _filter_infringing(results: list, is_admin: bool) -> list:
    if is_admin:
        return results
    return [r for r in results if not db.is_infringing(r.get('infohash', ''))]


def _load_fingerprint_db():
    fingerprint_count = copyright_db.load(FINGERPRINT_DB_PATH)
    if fingerprint_count > 0:
        print(f"[Copyright] Loaded {fingerprint_count} fingerprints from {FINGERPRINT_DB_PATH}")
    db_fingerprints = db.get_all_fingerprints()
    if db_fingerprints:
        for fp in db_fingerprints:
            copyright_db.add_fingerprint(
                {k: fp[k] for k in fp if k in ('md5', 'sha1', 'sha256', 'phash', 'size', 'file_size', 'filename')},
                copyright_holder=fp.get('copyright_holder', 'Unknown'),
                title=fp.get('title', '')
            )
        print(f"[Copyright] Total fingerprints in memory: {copyright_db.count()}")
    print(f"[Copyright] pHash threshold: {db.COPYRIGHT_THRESHOLD:.0%}")


@app.route('/')
def index():
    frontend_dist = os.path.join(os.path.dirname(__file__), '..', '..', 'frontend', 'dist')
    if os.path.exists(os.path.join(frontend_dist, 'index.html')):
        return send_from_directory(frontend_dist, 'index.html')
    return jsonify({
        'service': 'BT Network Resource Monitor API',
        'version': '3.0.0',
        'features': [
            'DHT Infohash Crawling',
            'Smart Metadata Scheduling',
            'Bloom Filter Deduplication',
            'Invalid Infohash Blacklist (24h TTL)',
            '15s Timeout + 2 Retries on Metadata Download',
            'Failure Rate Auto-Recovery',
            'FTS5 Full-Text Search',
            'pHash Copyright Detection',
            'DMCA Takedown Generation',
            'Infringement Statistics'
        ],
        'endpoints': {
            'resources': '/api/resources',
            'search': '/api/search',
            'hot': '/api/hot',
            'magnet_submit': '/api/magnet/submit',
            'stats': '/api/stats',
            'copyright_stats': '/api/copyright/stats',
            'infringements': '/api/copyright/infringements',
            'dmca': '/api/copyright/dmca'
        }
    })


@app.route('/api/infohash', methods=['POST'])
def receive_infohash():
    data = request.get_json(force=True)
    if not data:
        return jsonify({'error': 'Invalid request body'}), 400

    infohash = data.get('infohash', '').lower().strip()
    source_ip = data.get('source_ip', '')
    source_port = data.get('source_port', 0)
    timestamp = data.get('timestamp', int(time.time()))
    event_type = data.get('event_type', 'get_peers')
    announce_count = data.get('announce_count', 1)

    if not infohash or len(infohash) != 40:
        return jsonify({'error': 'Invalid infohash format'}), 400

    if db.is_blacklisted(infohash):
        return jsonify({'status': 'blacklisted', 'infohash': infohash}), 200

    if db.has_been_tried(infohash) and db.infohash_exists(infohash):
        db.update_download_count(infohash)
        return jsonify({'status': 'duplicate', 'infohash': infohash}), 200

    success = db.insert_infohash(infohash, source_ip, source_port, timestamp)
    if success:
        db.update_download_count(infohash)

        if event_type == 'announce' or event_type == 'get_peers':
            estimator = DHTSeederEstimator()
            estimated = estimator.estimate(infohash, announce_count)
            db.update_estimated_seeders(infohash, estimated)

        with _announce_lock:
            _announce_count[infohash] = _announce_count.get(infohash, 0) + announce_count

    return jsonify({'status': 'ok', 'infohash': infohash}), 201


@app.route('/api/resources', methods=['GET'])
def get_resources():
    page = max(1, int(request.args.get('page', 1)))
    per_page = min(100, max(1, int(request.args.get('per_page', 20))))
    status = request.args.get('status', None)
    is_admin = _is_admin_request()

    result = db.get_resources(page=page, per_page=per_page, status=status)
    result['items'] = _filter_infringing(result.get('items', []), is_admin)
    return jsonify(result)


@app.route('/api/resources/<infohash>', methods=['GET'])
def get_resource_detail(infohash):
    detail = db.get_resource_detail(infohash)
    if not detail:
        return jsonify({'error': 'Resource not found'}), 404

    if detail.get('files'):
        try:
            detail['files'] = json.loads(detail['files'])
        except (json.JSONDecodeError, TypeError):
            pass

    if detail.get('piece_hashes'):
        try:
            detail['piece_hashes'] = json.loads(detail['piece_hashes'])
        except (json.JSONDecodeError, TypeError):
            pass

    detail['is_infringing'] = db.is_infringing(infohash)
    if detail['is_infringing']:
        detail['infringement_records'] = db.get_infringement_records(
            copyright_holder=None,
            limit=10
        )

    return jsonify(detail)


@app.route('/api/search', methods=['GET'])
def search_torrents():
    query = request.args.get('q', '').strip()
    if not query:
        return jsonify({'error': 'Search query is required'}), 400

    is_admin = _is_admin_request()
    results = db.search_torrents(query)
    results = _filter_infringing(results, is_admin)

    return jsonify({'query': query, 'total': len(results), 'results': results})


@app.route('/api/hot', methods=['GET'])
def get_hot_resources():
    hours = int(request.args.get('hours', 24))
    limit = min(500, int(request.args.get('limit', 100)))
    is_admin = _is_admin_request()

    resources = db.get_hot_resources(hours=hours, limit=limit)
    resources = _filter_infringing(resources, is_admin)

    return jsonify({'time_window_hours': hours, 'total': len(resources), 'resources': resources})


@app.route('/api/magnet/submit', methods=['POST'])
def submit_magnet():
    data = request.get_json(force=True)
    if not data:
        return jsonify({'error': 'Invalid request body'}), 400

    magnet_uri = data.get('magnet_uri', '').strip()
    if not magnet_uri:
        return jsonify({'error': 'magnet_uri is required'}), 400

    infohash = MagnetParser.extract_infohash(magnet_uri)
    if not infohash:
        return jsonify({'error': 'Invalid magnet URI, could not extract infohash'}), 400

    if db.is_blacklisted(infohash):
        return jsonify({'error': 'This infohash is in the blacklist (24h TTL)'}), 410

    if not db.infohash_exists(infohash):
        db.insert_infohash(infohash, 'user_submit', 0, int(time.time()))

    parsed = MagnetParser.parse_magnet(magnet_uri)
    if parsed:
        display_name = parsed.get('display_name') or f'Unknown ({infohash[:8]}...)'
        db.upsert_torrent_meta(
            infohash=infohash,
            name=display_name,
            total_size=0,
            piece_length=0,
            piece_count=0,
            piece_hashes=[],
            files=[display_name]
        )

    metadata = None
    copyright_check = None
    if data.get('fetch_metadata', False):
        metadata = MagnetParser.fetch_metadata(
            infohash,
            timeout=db.METADATA_TIMEOUT,
            max_retries=db.MAX_RETRY
        )
        if metadata:
            db.upsert_torrent_meta(
                infohash=infohash,
                name=metadata['name'],
                total_size=metadata['total_size'],
                piece_length=metadata['piece_length'],
                piece_count=metadata['piece_count'],
                piece_hashes=[h.hex() for h in metadata['piece_hashes']],
                files=[f"{f['path']} ({f['size']} bytes)" for f in metadata['files']]
            )
            db.record_success(infohash)

            copyright_check = _check_copyright(infohash, metadata)
        else:
            db.record_failure(infohash)
            db.mark_invalid(infohash, "user_submit_timeout")

    response = {
        'status': 'ok',
        'infohash': infohash,
        'parsed': parsed,
        'metadata_fetched': metadata is not None
    }
    if metadata:
        response['metadata'] = {
            'name': metadata['name'],
            'total_size': metadata['total_size'],
            'file_count': len(metadata['files'])
        }
    if copyright_check:
        response['copyright_check'] = copyright_check

    return jsonify(response), 201


@app.route('/api/stats', methods=['GET'])
def get_stats():
    stats = db.get_stats()
    stats['crawl_interval'] = CRAWL_INTERVAL
    estimator = DHTSeederEstimator()
    stats['estimated_seeders_cached'] = len(estimator._seeder_cache)

    copyright_stats = db.get_copyright_stats()
    stats['copyright'] = copyright_stats
    return jsonify(stats)


@app.route('/api/copyright/stats', methods=['GET'])
def get_copyright_stats():
    stats = db.get_copyright_stats()
    stats['by_holder'] = db.get_infringement_stats_by_holder()
    stats['fingerprint_db_loaded'] = copyright_db.count()
    return jsonify(stats)


@app.route('/api/copyright/infringements', methods=['GET'])
def get_infringements():
    if not _is_admin_request():
        return jsonify({'error': 'Admin access required'}), 403

    status = request.args.get('status', None)
    copyright_holder = request.args.get('holder', None)
    limit = min(500, int(request.args.get('limit', 100)))

    records = db.get_infringement_records(
        status=status, copyright_holder=copyright_holder, limit=limit
    )

    for r in records:
        if r.get('evidence_json'):
            try:
                r['evidence'] = json.loads(r['evidence_json'])
            except (json.JSONDecodeError, TypeError):
                pass

    return jsonify({'total': len(records), 'records': records})


@app.route('/api/copyright/infringements/<int:record_id>/status', methods=['PUT'])
def update_infringement_status(record_id):
    if not _is_admin_request():
        return jsonify({'error': 'Admin access required'}), 403

    data = request.get_json(force=True)
    if not data:
        return jsonify({'error': 'Invalid request body'}), 400

    status = data.get('status', '')
    if status not in ('confirmed', 'false_positive', 'suspected', 'resolved'):
        return jsonify({'error': 'Invalid status. Must be confirmed, false_positive, suspected, or resolved'}), 400

    db.mark_infringement_status(record_id, status)
    return jsonify({'status': 'ok', 'record_id': record_id, 'new_status': status})


@app.route('/api/copyright/dmca/generate', methods=['POST'])
def generate_dmca_notice():
    if not _is_admin_request():
        return jsonify({'error': 'Admin access required'}), 403

    data = request.get_json(force=True)
    if not data:
        return jsonify({'error': 'Invalid request body'}), 400

    record_id = data.get('record_id')
    if not record_id:
        return jsonify({'error': 'record_id is required'}), 400

    records = db.get_infringement_records(limit=1)
    target_record = None
    for r in records:
        if r['id'] == record_id:
            target_record = r
            break

    if not target_record:
        all_records = db.get_infringement_records(limit=500)
        for r in all_records:
            if r['id'] == record_id:
                target_record = r
                break

    if not target_record:
        return jsonify({'error': 'Infringement record not found'}), 404

    sender_info = {
        'name': data.get('sender_name', os.environ.get('DMCA_SENDER_NAME', 'Copyright Agent')),
        'email': data.get('sender_email', os.environ.get('DMCA_SENDER_EMAIL', 'dmca@example.com')),
        'phone': data.get('sender_phone', os.environ.get('DMCA_SENDER_PHONE', '')),
        'address': data.get('sender_address', os.environ.get('DMCA_SENDER_ADDRESS', ''))
    }

    notice = DMCAGenerator.generate_notice(target_record, sender_info, {
        'title': target_record.get('matched_title', 'Unknown Work'),
        'copyright_holder': target_record.get('copyright_holder', 'Unknown'),
        'original_source': target_record.get('matched_source', 'Unknown'),
        'recipient_email': data.get('recipient_email', 'abuse@example.com')
    })

    subject = f"DMCA Takedown Notice - {target_record.get('matched_title', 'Unknown Work')}"

    notice_id = db.save_dmca_notice(
        record_id, target_record['infohash'],
        data.get('recipient_email', 'abuse@example.com'),
        subject, notice
    )

    return jsonify({
        'status': 'ok',
        'notice_id': notice_id,
        'subject': subject,
        'notice': notice,
        'record': target_record
    })


@app.route('/api/copyright/dmca/batch', methods=['POST'])
def generate_batch_dmca():
    if not _is_admin_request():
        return jsonify({'error': 'Admin access required'}), 403

    data = request.get_json(force=True)
    copyright_holder = data.get('copyright_holder')
    record_ids = data.get('record_ids', [])

    sender_info = {
        'name': data.get('sender_name', os.environ.get('DMCA_SENDER_NAME', 'Copyright Agent')),
        'email': data.get('sender_email', os.environ.get('DMCA_SENDER_EMAIL', 'dmca@example.com')),
        'phone': data.get('sender_phone', ''),
        'address': data.get('sender_address', '')
    }

    if copyright_holder:
        records = db.get_infringement_records(
            copyright_holder=copyright_holder, limit=500
        )
    elif record_ids:
        all_records = db.get_infringement_records(limit=500)
        records = [r for r in all_records if r['id'] in record_ids]
    else:
        return jsonify({'error': 'Either copyright_holder or record_ids is required'}), 400

    notices = DMCAGenerator.generate_batch_notices(records, sender_info, group_by_holder=True)

    notice_ids = []
    for holder, holder_notices in notices.items():
        for i, notice_text in enumerate(holder_notices):
            if i < len(records):
                record = records[i]
                subject = f"DMCA Takedown Notice - {record.get('matched_title', 'Unknown Work')}"
                nid = db.save_dmca_notice(
                    record['id'], record['infohash'],
                    data.get('recipient_email', 'abuse@example.com'),
                    subject, notice_text
                )
                notice_ids.append(nid)

    return jsonify({
        'status': 'ok',
        'total_notices': len(notice_ids),
        'notice_ids': notice_ids,
        'grouped_by_holder': {h: len(n) for h, n in notices.items()}
    })


@app.route('/api/copyright/dmca/<int:notice_id>/send', methods=['POST'])
def mark_dmca_sent(notice_id):
    if not _is_admin_request():
        return jsonify({'error': 'Admin access required'}), 403

    notices = db.get_dmca_notices(limit=500)
    found = False
    for n in notices:
        if n['id'] == notice_id:
            found = True
            break

    if not found:
        return jsonify({'error': 'DMCA notice not found'}), 404

    return jsonify({'status': 'ok', 'notice_id': notice_id, 'sent': True})


@app.route('/api/copyright/dmca', methods=['GET'])
def get_dmca_notices():
    if not _is_admin_request():
        return jsonify({'error': 'Admin access required'}), 403

    infringement_id = request.args.get('infringement_id', type=int)
    limit = min(200, int(request.args.get('limit', 50)))
    notices = db.get_dmca_notices(infringement_id=infringement_id, limit=limit)
    return jsonify({'total': len(notices), 'notices': notices})


@app.route('/api/copyright/fingerprints', methods=['GET'])
def get_fingerprints():
    if not _is_admin_request():
        return jsonify({'error': 'Admin access required'}), 403

    fingerprints = db.get_all_fingerprints()
    return jsonify({'total': len(fingerprints), 'fingerprints': fingerprints})


@app.route('/api/copyright/fingerprints/import', methods=['POST'])
def import_fingerprints():
    if not _is_admin_request():
        return jsonify({'error': 'Admin access required'}), 403

    data = request.get_json(force=True)
    if not data:
        return jsonify({'error': 'Invalid request body'}), 400

    fingerprints = data.get('fingerprints', [])
    count = 0
    for fp in fingerprints:
        success = copyright_db.add_fingerprint(
            fp,
            copyright_holder=fp.get('copyright_holder', 'Unknown'),
            title=fp.get('title', fp.get('filename', ''))
        )
        if success:
            db.import_fingerprint_db.__wrapped__  # no-op
            count += 1

    if copyright_db.count() > 0:
        copyright_db.save(FINGERPRINT_DB_PATH)

    return jsonify({'status': 'ok', 'imported': count, 'total': copyright_db.count()})


@app.route('/api/health/<infohash>', methods=['POST'])
def update_health(infohash):
    data = request.get_json(force=True)
    if not data:
        return jsonify({'error': 'Invalid request body'}), 400

    seeders = int(data.get('seeders', 0))
    leechers = int(data.get('leechers', 0))
    db.update_health(infohash, seeders, leechers)

    if seeders > 0:
        estimator = DHTSeederEstimator()
        estimator.update_estimate(infohash, seeders)
        db.update_estimated_seeders(infohash, seeders)

    return jsonify({'status': 'ok', 'infohash': infohash,
                    'seeders': seeders, 'leechers': leechers})


@app.route('/api/blacklist/cleanup', methods=['POST'])
def cleanup_blacklist():
    count = db.cleanup_expired_blacklist()
    db.reset_failure_stats()
    return jsonify({'cleaned_count': count})


def _check_copyright(infohash: str, metadata: dict) -> dict:
    if not metadata or not metadata.get('files'):
        return None

    file_list = metadata.get('files', [])
    if not file_list:
        return None

    total_size = metadata.get('total_size', 0)
    if total_size == 0:
        return None

    name = metadata.get('name', '')
    phash = fingerprint_engine.phash.compute_bytes_phash(
        name.encode('utf-8') + str(total_size).encode('utf-8')
    )

    matches = copyright_db.find_matches(
        {'phash': phash, 'md5': '', 'size': total_size},
        threshold=db.COPYRIGHT_THRESHOLD
    )

    if not matches:
        return {'infringing': False, 'phash': phash}

    best_match, similarity = matches[0]

    source_ip = ''
    detail = db.get_resource_detail(infohash)
    if detail:
        source_ip = detail.get('source_ip', '')

    file_names = ', '.join([f.get('path', f.get('name', '')) for f in file_list[:5]])
    if len(file_list) > 5:
        file_names += f' ... (+{len(file_list) - 5} more)'

    record_id = db.record_infringement(
        infohash=infohash,
        file_name=file_names,
        file_size=total_size,
        phash=phash,
        matched_fp=best_match,
        similarity=similarity,
        uploader_ip=None,
        source_ip=source_ip
    )

    print(f"[Copyright] Infringement detected: {infohash} -> "
          f"{best_match.get('title', 'Unknown')} "
          f"({similarity:.1%} similar)")

    return {
        'infringing': True,
        'record_id': record_id,
        'phash': phash,
        'similarity': similarity,
        'matched_title': best_match.get('title', ''),
        'copyright_holder': best_match.get('copyright_holder', '')
    }


def _should_switch_dht_bootstrap() -> bool:
    rate = db.get_failure_rate()
    return rate >= db.FAILURE_RATE_THRESHOLD


def run_background_parser():
    print(f"[Background] Smart parser started. timeout={db.METADATA_TIMEOUT}s, "
          f"max_retries={db.MAX_RETRY}, blacklist_ttl={db.BLACKLIST_TTL_SECONDS}s")
    print(f"[Background] Copyright detection: threshold={db.COPYRIGHT_THRESHOLD:.0%}, "
          f"fingerprints={copyright_db.count()}")

    consecutive_failures = 0
    parse_interval = max(CRAWL_INTERVAL, 15)

    while True:
        try:
            failure_rate = db.get_failure_rate()
            if failure_rate > 0:
                print(f"[Background] Failure rate: {failure_rate:.1%}")

            if _should_switch_dht_bootstrap():
                print(f"[Background] WARNING: Failure rate {failure_rate:.1%} exceeds "
                      f"threshold {db.FAILURE_RATE_THRESHOLD:.1%}. "
                      f"Recommend DHT bootstrap switch.")
                db.reset_failure_stats()
                consecutive_failures = 0

            pending = db.get_pending_resources_smart(limit=30)
            items = pending.get('items', [])

            if not items:
                print("[Background] No pending resources to parse")
                time.sleep(parse_interval)
                continue

            print(f"[Background] Processing {len(items)} pending resources "
                  f"(smart order by estimated seeders)")

            success_count = 0
            for item in items:
                infohash = item['infohash']

                if db.is_blacklisted(infohash):
                    continue

                if not db.should_retry(infohash):
                    print(f"[Background] Skipping {infohash}: max retries reached")
                    db.mark_invalid(infohash, "max_retries_exceeded")
                    continue

                if item.get('name') and item.get('total_size', 0) > 0:
                    continue

                retry_num = db.get_retry_count(infohash)
                print(f"[Background] Parsing {infohash} "
                      f"(retry {retry_num + 1}/{db.MAX_RETRY}, "
                      f"timeout {db.METADATA_TIMEOUT}s)")

                metadata = MagnetParser.fetch_metadata(
                    infohash,
                    timeout=db.METADATA_TIMEOUT,
                    max_retries=db.MAX_RETRY - retry_num
                )

                if metadata:
                    db.upsert_torrent_meta(
                        infohash=infohash,
                        name=metadata['name'],
                        total_size=metadata['total_size'],
                        piece_length=metadata['piece_length'],
                        piece_count=metadata['piece_count'],
                        piece_hashes=[h.hex() for h in metadata['piece_hashes']],
                        files=[f"{f['path']} ({f['size']} bytes)" for f in metadata['files']]
                    )
                    db.record_success(infohash)
                    success_count += 1
                    consecutive_failures = 0

                    name_short = metadata['name'][:50] if metadata['name'] else 'unknown'
                    print(f"[Background] Parsed: {infohash} -> "
                          f"{name_short} "
                          f"({db._human_readable_size(metadata['total_size'])}, "
                          f"{len(metadata['files'])} files)")

                    if copyright_db.count() > 0:
                        _check_copyright(infohash, metadata)
                else:
                    db.record_failure(infohash)
                    consecutive_failures += 1
                    db.mark_invalid(infohash, "metadata_timeout")
                    print(f"[Background] Failed: {infohash} (timeout)")

            cleaned = db.cleanup_expired_blacklist()
            if cleaned > 0:
                print(f"[Background] Cleaned {cleaned} expired blacklist entries")

            print(f"[Background] Cycle complete: {success_count} success, "
                  f"{len(items) - success_count} failed")

        except Exception as e:
            print(f"[Background] Error: {e}")
            import traceback
            traceback.print_exc()

        time.sleep(parse_interval)


def signal_handler(sig, frame):
    print('\n[API] Shutting down...')
    sys.exit(0)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='BT Network Monitor API')
    parser.add_argument('--host', default=None, help='Bind host')
    parser.add_argument('--port', type=int, default=None, help='Bind port')
    parser.add_argument('--crawl-interval', type=int, default=None,
                        help='Crawl interval in seconds (default: 60)')
    parser.add_argument('--fingerprint-db', default=None,
                        help='Path to copyright fingerprint database JSON file')
    parser.add_argument('--admin-token', default=None,
                        help='Admin API token for copyright management')
    args = parser.parse_args()

    if args.crawl_interval:
        CRAWL_INTERVAL = args.crawl_interval
    if args.fingerprint_db:
        FINGERPRINT_DB_PATH = args.fingerprint_db
        copyright_db.db_path = args.fingerprint_db
    if args.admin_token:
        ADMIN_TOKEN = args.admin_token

    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)

    _load_fingerprint_db()

    bg_thread = threading.Thread(target=run_background_parser, daemon=True)
    bg_thread.start()

    host = args.host or os.environ.get('API_HOST', '0.0.0.0')
    port = args.port or int(os.environ.get('API_PORT', '5000'))

    print(f"[API] Starting BT Monitor API v3.0 on {host}:{port}")
    print(f"[API] Crawl interval: {CRAWL_INTERVAL}s")
    print(f"[API] Metadata timeout: {db.METADATA_TIMEOUT}s, max retries: {db.MAX_RETRY}")
    print(f"[API] Blacklist TTL: {db.BLACKLIST_TTL_SECONDS}s")
    print(f"[API] Failure rate threshold: {db.FAILURE_RATE_THRESHOLD}")
    print(f"[API] Copyright threshold: {db.COPYRIGHT_THRESHOLD:.0%}")
    print(f"[API] Admin token: {'set' if ADMIN_TOKEN else 'not set'}")
    app.run(host=host, port=port, debug=False)
