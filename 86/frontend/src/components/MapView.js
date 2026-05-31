import React, { useEffect, useRef, useState } from 'react';
import mapboxgl from 'mapbox-gl';

mapboxgl.accessToken = process.env.REACT_APP_MAPBOX_TOKEN || 'pk.eyJ1IjoiZGVtby11c2VyIiwiYSI6ImNtMG9hZmw5dDBiaWUyanM5ZjRmeG11dHYifQ.fake-token-for-demo';

function MapView({ 
  drones, 
  formation, 
  selectedDrone, 
  onSelectDrone, 
  isReplayMode,
  noFlyZones,
  dynamicObstacles,
  plannedPath,
  formationState,
  onMapClick
}) {
  const mapContainer = useRef(null);
  const map = useRef(null);
  const markers = useRef({});
  const obstacleMarkers = useRef({});
  const [mapLoaded, setMapLoaded] = useState(false);
  const [mapCenter, setMapCenter] = useState([121.4737, 31.2304]);

  useEffect(() => {
    if (map.current) return;

    map.current = new mapboxgl.Map({
      container: mapContainer.current,
      style: 'mapbox://styles/mapbox/satellite-streets-v12',
      center: mapCenter,
      zoom: 16,
      pitch: 45
    });

    map.current.addControl(new mapboxgl.NavigationControl(), 'top-right');
    map.current.addControl(new mapboxgl.ScaleControl(), 'bottom-left');

    map.current.on('load', () => {
      setMapLoaded(true);
    });

    map.current.on('click', (e) => {
      if (onMapClick) {
        onMapClick({
          lat: e.lngLat.lat,
          lng: e.lngLat.lng,
          alt: 10
        });
      }
    });

    return () => {
      map.current?.remove();
    };
  }, []);

  useEffect(() => {
    if (!mapLoaded || !map.current) return;

    drones.forEach(drone => {
      if (!drone.position) return;

      const modeColor = drone.positionMode === 'gps' ? 'gps' : 
                        drone.positionMode === 'optical_flow' ? 'optical' : 'imu';
      
      const el = document.createElement('div');
      el.className = 'drone-marker';
      el.innerHTML = `
        <div class="drone-icon ${drone.connected ? 'connected' : 'disconnected'} ${drone.armed ? 'armed' : ''} ${drone.locked ? 'locked' : ${drone.gpsLost ? 'gps-lost' : ''} ${modeColor}">
          ${drone.id.split('-')[1]}
          ${drone.positionMode !== 'gps' ? `<span class="mode-badge">${drone.positionMode === 'optical_flow' ? '光' : '惯'}</span>` : ''}
        </div>
      `;
      el.addEventListener('click', () => onSelectDrone(drone.id));

      if (markers.current[drone.id]) {
        markers.current[drone.id].remove();
      }

      markers.current[drone.id] = new mapboxgl.Marker(el)
        .setLngLat([drone.position.lng, drone.position.lat])
        .addTo(map.current);
    });

    Object.keys(markers.current).forEach(id => {
      if (!drones.find(d => d.id === id)) {
        markers.current[id].remove();
        delete markers.current[id];
      }
    });
  }, [drones, mapLoaded, onSelectDrone]);

  useEffect(() => {
    if (!mapLoaded || !map.current) return;

    dynamicObstacles.forEach(obstacle => {
      const el = document.createElement('div');
      el.className = 'obstacle-marker';
      el.innerHTML = `
        <div class="obstacle-icon ${obstacle.type}">
          <span>⚠</span>
        </div>
      `;

      if (obstacleMarkers.current[obstacle.id]) {
        obstacleMarkers.current[obstacle.id].remove();
      }

      obstacleMarkers.current[obstacle.id] = new mapboxgl.Marker(el)
        .setLngLat([obstacle.position.lng, obstacle.position.lat])
        .addTo(map.current);
    });

    Object.keys(obstacleMarkers.current).forEach(id => {
      if (!dynamicObstacles.find(o => o.id === id)) {
        obstacleMarkers.current[id].remove();
        delete obstacleMarkers.current[id];
      }
    });
  }, [dynamicObstacles, mapLoaded]);

  useEffect(() => {
    if (!mapLoaded || !map.current) return;

    if (map.current.getSource('formation-line')) {
      map.current.removeLayer('formation-line');
      map.current.removeSource('formation-line');
    }

    if (map.current.getSource('planned-path')) {
      map.current.removeLayer('planned-path');
      map.current.removeSource('planned-path');
    }

    if (formation && formation.positions && formation.positions.length > 1) {
      const coordinates = formation.positions.map(pos => [pos.lng, pos.lat]);
      coordinates.push(coordinates[0]);

      const lineColor = formationState?.isAvoiding ? '#fa8c16' : '#1890ff';
      const lineWidth = formationState?.isAvoiding ? 3 : 2;

      map.current.addSource('formation-line', {
        type: 'geojson',
        data: {
          type: 'Feature',
          properties: {},
          geometry: {
            type: 'LineString',
            coordinates: coordinates
          }
        }
      });

      map.current.addLayer({
        id: 'formation-line',
        type: 'line',
        source: 'formation-line',
        layout: {
          'line-join': 'round',
          'line-cap': 'round'
        },
        paint: {
          'line-color': lineColor,
          'line-width': lineWidth,
          'line-opacity': 0.7,
          'line-dasharray': [2, 2]
        }
      });
    }
  }, [formation, formationState, mapLoaded]);

  useEffect(() => {
    if (!mapLoaded || !map.current) return;

    if (map.current.getSource('planned-path')) {
      map.current.removeLayer('planned-path');
      map.current.removeSource('planned-path');
    }

    if (plannedPath && plannedPath.length > 1) {
      const coordinates = plannedPath.map(pos => [pos.lng, pos.lat]);

      map.current.addSource('planned-path', {
        type: 'geojson',
        data: {
          type: 'Feature',
          properties: {},
          geometry: {
            type: 'LineString',
            coordinates: coordinates
          }
        }
      });

      map.current.addLayer({
        id: 'planned-path',
        type: 'line',
        source: 'planned-path',
        layout: {
          'line-join': 'round',
          'line-cap': 'round'
        },
        paint: {
          'line-color': '#52c41a',
          'line-width': 3,
          'line-opacity': 0.8,
          'line-dasharray': [4, 4]
        }
      });
    }
  }, [plannedPath, mapLoaded]);

  useEffect(() => {
    if (!mapLoaded || !map.current) return;

    noFlyZones.forEach((zone, index) => {
      const sourceId = `nfz-${zone.id || index}`;
      const layerId = `nfz-layer-${zone.id || index}`;

      if (map.current.getSource(sourceId)) {
        map.current.removeLayer(layerId);
        map.current.removeSource(sourceId);
      }

      let geometry;
      if (zone.type === 'circle') {
        const center = zone.center;
        const radius = zone.radius;
        const points = [];
        const steps = 64;
        
        for (let i = 0; i < steps; i++) {
          const angle = (i / steps) * 2 * Math.PI;
          const lat = center.lat + (radius / 111000) * Math.cos(angle);
          const lng = center.lng + (radius / (111000 * Math.cos(center.lat * Math.PI / 180))) * Math.sin(angle);
          points.push([lng, lat]);
        }
        points.push(points[0]);
        
        geometry = {
          type: 'Polygon',
          coordinates: [points]
        };
      } else if (zone.type === 'polygon' && zone.coordinates) {
        const coordinates = zone.coordinates.map(c => [c.lng, c.lat]);
        coordinates.push(coordinates[0]);
        geometry = {
          type: 'Polygon',
          coordinates: [coordinates]
        };
      }

      if (geometry) {
        map.current.addSource(sourceId, {
          type: 'geojson',
          data: {
            type: 'Feature',
            properties: { name: zone.name },
            geometry
          }
        });

        map.current.addLayer({
          id: layerId,
          type: 'fill',
          source: sourceId,
          paint: {
            'fill-color': '#ff4d4f',
            'fill-opacity': 0.3
          }
        });

        map.current.addLayer({
          id: `${layerId}-outline`,
          type: 'line',
          source: sourceId,
          paint: {
            'line-color': '#ff4d4f',
            'line-width': 2,
            'line-opacity': 0.8
          }
        });
      }
    });
  }, [noFlyZones, mapLoaded]);

  useEffect(() => {
    if (!mapLoaded || !map.current || !selectedDrone) return;

    const drone = drones.find(d => d.id === selectedDrone);
    if (drone && drone.position) {
      map.current.easeTo({
        center: [drone.position.lng, drone.position.lat],
        duration: 500
      });
    }
  }, [selectedDrone, drones, mapLoaded]);

  const getFormationStateText = () => {
    if (!formationState) return '正常';
    if (formationState.isFormationBroken) return '编队解散（避障中）';
    if (formationState.isAvoiding && formationState.avoidReason === 'gps_degradation') return 'GPS降级模式';
    if (formationState.isAvoiding) return '避障中';
    return '正常';
  };

  const getFormationStateColor = () => {
    if (!formationState) return '#52c41a';
    if (formationState.isFormationBroken) return '#ff4d4f';
    if (formationState.isAvoiding) return '#fa8c16';
    return '#52c41a';
  };

  return (
    <div ref={mapContainer} className="map-container">
      {drones.length > 0 && (
        <div className="map-overlay">
          <h4>系统状态</h4>
          <div className="stat">
            <span>在线无人机:</span>
            <span>{drones.filter(d => d.connected).length}/{drones.length}</span>
          </div>
          <div className="stat">
            <span>已起飞:</span>
            <span>{drones.filter(d => d.armed).length}</span>
          </div>
          <div className="stat">
            <span>已锁定:</span>
            <span>{drones.filter(d => d.locked).length}</span>
          </div>
          <div className="stat">
            <span>GPS异常:</span>
            <span style={{ color: drones.some(d => d.gpsLost) ? '#fa8c16' : '#52c41a' }}>
              {drones.filter(d => d.gpsLost).length}
            </span>
          </div>
          <div className="stat">
            <span>当前队形:</span>
            <span>{formation ? {
              line: '一字形',
              v_shape: 'V字形',
              circle: '圆形',
              triangle: '三角形'
            }[formation.type] : '无'}</span>
          </div>
          <div className="stat">
            <span>编队状态:</span>
            <span style={{ color: getFormationStateColor() }}>{getFormationStateText()}</span>
          </div>
          {formationState?.avoidReason && (
            <div className="stat" style={{ color: '#fa8c16' }}>
              <span>原因:</span>
              <span>{
                formationState.avoidReason === 'gps_degradation' ? 'GPS信号丢失' :
                formationState.avoidReason === 'dynamic_obstacle' ? '动态障碍物' :
                formationState.avoidReason === 'nfz_violation' ? '禁飞区违规' :
                formationState.avoidReason
              }</span>
            </div>
          )}
          {formationState?.gpsDegradedDrones?.length > 0 && (
            <div className="stat" style={{ color: '#fa8c16', fontSize: '11px' }}>
              <span>受影响:</span>
              <span>{formationState.gpsDegradedDrones.join(', ')}</span>
            </div>
          )}
          {formationState?.formationSpeedFactor !== undefined && formationState.formationSpeedFactor !== 1 && (
            <div className="stat" style={{ color: '#fa8c16' }}>
              <span>速度因子:</span>
              <span>{formationState.formationSpeedFactor.toFixed(1)}x</span>
            </div>
          )}
          {noFlyZones.length > 0 && (
            <div className="stat">
              <span>禁飞区:</span>
              <span>{noFlyZones.length}个</span>
            </div>
          )}
          {dynamicObstacles.length > 0 && (
            <div className="stat" style={{ color: '#fa8c16' }}>
              <span>动态障碍物:</span>
              <span>{dynamicObstacles.length}个</span>
            </div>
          )}
          {isReplayMode && (
            <div className="stat" style={{ color: '#fa8c16' }}>
              <span>回放模式</span>
              <span>●</span>
            </div>
          )}
        </div>
      )}
      
      <style>{`
        .drone-icon {
          width: 32px;
          height: 32px;
          border-radius: 50%;
          display: flex;
          align-items: center;
          justify-content: center;
          font-weight: bold;
          font-size: 12px;
          color: white;
          position: relative;
          border: 2px solid white;
          box-shadow: 0 2px 8px rgba(0,0,0,0.3);
          transition: all 0.3s ease;
        }
        .drone-icon.connected { background: #52c41a; }
        .drone-icon.disconnected { background: #8c8c8c; }
        .drone-icon.armed { animation: pulse 1.5s infinite; }
        .drone-icon.locked { background: #fa8c16; }
        .drone-icon.gps-lost { background: #ff4d4f; }
        .drone-icon.gps { border-color: #52c41a; }
        .drone-icon.optical { border-color: #fa8c16; }
        .drone-icon.imu { border-color: #722ed1; }
        .mode-badge {
          position: absolute;
          top: -6px;
          right: -6px;
          width: 16px;
          height: 16px;
          border-radius: 50%;
          background: #fa8c16;
          font-size: 9px;
          display: flex;
          align-items: center;
          justify-content: center;
        }
        .obstacle-icon {
          width: 24px;
          height: 24px;
          border-radius: 50%;
          background: #ff4d4f;
          display: flex;
          align-items: center;
          justify-content: center;
          color: white;
          font-size: 14px;
          animation: blink 1s infinite;
        }
        @keyframes pulse {
          0%, 100% { transform: scale(1); box-shadow: 0 2px 8px rgba(0,0,0,0.3); }
          50% { transform: scale(1.1); box-shadow: 0 4px 16px rgba(82, 196, 26, 0.6); }
        }
        @keyframes blink {
          0%, 100% { opacity: 1; }
          50% { opacity: 0.5; }
        }
      `}</style>
    </div>
  );
}

export default MapView;
