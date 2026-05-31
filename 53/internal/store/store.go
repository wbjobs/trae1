package store

import "github.com/syndtr/goleveldb/leveldb"

type Store interface {
	Get(key []byte) ([]byte, error)
	Put(key, value []byte) error
	Delete(key []byte) error
	ForEach(fn func(k, v []byte) bool) error
	Close() error
}

type LevelDBStore struct {
	path string
	db   *leveldb.DB
}

func NewLevelDBStore(path string) *LevelDBStore {
	return &LevelDBStore{path: path}
}

func (s *LevelDBStore) Open() error {
	db, err := leveldb.OpenFile(s.path, nil)
	if err != nil {
		return err
	}
	s.db = db
	return nil
}

func (s *LevelDBStore) Get(key []byte) ([]byte, error) {
	v, err := s.db.Get(key, nil)
	if err == leveldb.ErrNotFound {
		return nil, nil
	}
	return v, err
}

func (s *LevelDBStore) Put(key, value []byte) error {
	return s.db.Put(key, value, nil)
}

func (s *LevelDBStore) Delete(key []byte) error {
	return s.db.Delete(key, nil)
}

func (s *LevelDBStore) ForEach(fn func(k, v []byte) bool) error {
	iter := s.db.NewIterator(nil, nil)
	defer iter.Release()
	for iter.Next() {
		k := append([]byte(nil), iter.Key()...)
		v := append([]byte(nil), iter.Value()...)
		if !fn(k, v) {
			break
		}
	}
	return iter.Error()
}

func (s *LevelDBStore) Close() error {
	if s.db == nil {
		return nil
	}
	return s.db.Close()
}
