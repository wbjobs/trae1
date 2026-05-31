package com.sharding.sync.incremental.service;

import com.sharding.sync.incremental.entity.BinlogPosition;
import com.sharding.sync.incremental.entity.IncrementalEvent;

import java.util.List;

public interface IncrementalService {

    BinlogPosition getPosition(String logicTable);

    void savePosition(BinlogPosition position);

    void saveEvent(IncrementalEvent event);

    List<IncrementalEvent> listPendingEvents(String logicTable, int limit);

    void markEventSuccess(Long eventId);

    void markEventFailed(Long eventId, String errorMsg);

    List<IncrementalEvent> listRecent(String logicTable, int limit);
}
