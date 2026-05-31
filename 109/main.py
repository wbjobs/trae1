#!/usr/bin/env python3
import asyncio
import argparse
import json
import yaml
import logging
import sys
from pythonjsonlogger import jsonlogger
from system_config import SystemConfig
from traffic_monitor import TrafficMonitor
from anomaly_detector import AnomalyDetector
from flowspec_controller import FlowSpecController
from rule_manager import RuleManager
from api_server import APIServer
from influxdb_writer import InfluxDBWriter
from ml_classifier import AttackClassifier
from feature_extractor import FeatureExtractor

def setup_logging():
    logger = logging.getLogger()
    logger.setLevel(logging.INFO)
    handler = logging.StreamHandler()
    formatter = jsonlogger.JsonFormatter(
        "%(asctime)s %(levelname)s %(message)s"
    )
    handler.setFormatter(formatter)
    logger.addHandler(handler)
    return logger

def load_config(config_path):
    with open(config_path, 'r') as f:
        return yaml.safe_load(f)

async def manual_escalate(rule_manager, dst_ip):
    success = await rule_manager.escalate_rule(dst_ip)
    if success:
        print(f"Successfully escalated rule for {dst_ip}")
    else:
        print(f"Failed to escalate rule for {dst_ip}")
    return success

def classify_samples(classifier, samples_file):
    print(f"Classifying samples from {samples_file}")
    try:
        with open(samples_file, 'r') as f:
            samples = json.load(f)
        
        results = []
        for sample in samples:
            features = sample.get('features', {})
            attack_type, confidence, strategy = classifier.classify_and_get_strategy(features)
            results.append({
                'sample_id': sample.get('id', 'unknown'),
                'attack_type': attack_type,
                'confidence': confidence,
                'strategy': strategy,
                'features': features
            })
            
            print(f"\nSample: {sample.get('id', 'unknown')}")
            print(f"  Attack Type: {attack_type}")
            print(f"  Confidence: {confidence:.2%}")
            print(f"  Strategy: {strategy.get('description', 'N/A')}")
            print(f"  Recommended Action: {strategy.get('action', 'N/A')}")
        
        return results
    except Exception as e:
        print(f"Error classifying samples: {e}")
        return []

async def main():
    parser = argparse.ArgumentParser(description="DDoS Traffic Diversion System")
    parser.add_argument("--config", default="config.yaml", help="Path to config file")
    parser.add_argument("--dry-run", action="store_true", help="Run in dry-run mode (no actual BGP changes)")
    parser.add_argument("--escalate", metavar="DST_IP", help="Manually escalate the rule for given destination IP and exit")
    parser.add_argument("--classify", metavar="SAMPLES_FILE", help="Classify traffic samples from JSON file and exit")
    parser.add_argument("--retrain", action="store_true", help="Retrain the ML model with existing data and exit")
    args = parser.parse_args()
    
    config = load_config(args.config)
    if args.dry_run:
        config['ddos_mitigator']['dry_run'] = True
    
    system_config = SystemConfig(config)
    logger = setup_logging()
    
    classifier = None
    if system_config.ml_enabled:
        classifier = AttackClassifier(system_config.config)
        classifier.initialize()
        logger.info("ML classifier initialized", extra={
            "model_path": system_config.ml_model_path,
            "target_accuracy": system_config.ml_target_accuracy
        })
    
    influxdb_writer = InfluxDBWriter(system_config)
    rule_manager = RuleManager(system_config, influxdb_writer)
    flowspec_controller = FlowSpecController(system_config, rule_manager)
    rule_manager.set_flowspec_controller(flowspec_controller)
    
    if args.classify:
        if not classifier:
            print("ML is not enabled in configuration")
            return
        results = classify_samples(classifier, args.classify)
        print(f"\nClassification complete: {len(results)} samples processed")
        return
    
    if args.retrain:
        if not classifier:
            print("ML is not enabled in configuration")
            return
        print("Retraining model...")
        success = classifier.train()
        if success:
            print("Model retrained successfully")
        else:
            print("Model retraining failed or accuracy below target")
        return
    
    if args.escalate:
        logger.info("Running manual escalation", extra={"dst_ip": args.escalate})
        await manual_escalate(rule_manager, args.escalate)
        return
    
    logger.info("Starting DDoS Traffic Diversion System", extra={
        "dry_run": system_config.dry_run,
        "ml_enabled": system_config.ml_enabled
    })
    
    anomaly_detector = AnomalyDetector(system_config, rule_manager, classifier)
    traffic_monitor = TrafficMonitor(system_config, anomaly_detector)
    api_server = APIServer(system_config, rule_manager)
    
    await asyncio.gather(
        traffic_monitor.start(),
        api_server.start(),
        rule_manager.cleanup_expired_rules()
    )

if __name__ == "__main__":
    asyncio.run(main())
