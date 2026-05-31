package store

import (
	"encoding/json"
	"fmt"

	"github.com/dgraph-io/badger/v4"
	"configvcs/pkg/version"
)

type Store struct {
	db *badger.DB
}

func New(path string) (*Store, error) {
	opts := badger.DefaultOptions(path)
	opts.Logger = nil
	db, err := badger.Open(opts)
	if err != nil {
		return nil, fmt.Errorf("open badger: %w", err)
	}
	return &Store{db: db}, nil
}

func (s *Store) Close() error {
	return s.db.Close()
}

func commitKey(id string) []byte {
	return []byte(fmt.Sprintf("commit:%s", id))
}

func branchKey(name string) []byte {
	return []byte(fmt.Sprintf("branch:%s", name))
}

func tagKey(name string) []byte {
	return []byte(fmt.Sprintf("tag:%s", name))
}

func headKey(branch string) []byte {
	return []byte(fmt.Sprintf("head:%s", branch))
}

func (s *Store) SaveCommit(c *version.Commit) error {
	data, err := json.Marshal(c)
	if err != nil {
		return err
	}
	return s.db.Update(func(txn *badger.Txn) error {
		if err := txn.Set(commitKey(c.ID), data); err != nil {
			return err
		}
		return txn.Set(headKey(c.Branch), []byte(c.ID))
	})
}

func (s *Store) GetCommit(id string) (*version.Commit, error) {
	var c version.Commit
	err := s.db.View(func(txn *badger.Txn) error {
		item, err := txn.Get(commitKey(id))
		if err != nil {
			return err
		}
		return item.Value(func(val []byte) error {
			return json.Unmarshal(val, &c)
		})
	})
	if err != nil {
		return nil, err
	}
	return &c, nil
}

func (s *Store) GetHead(branch string) (string, error) {
	var id string
	err := s.db.View(func(txn *badger.Txn) error {
		item, err := txn.Get(headKey(branch))
		if err != nil {
			return err
		}
		return item.Value(func(val []byte) error {
			id = string(val)
			return nil
		})
	})
	if err == badger.ErrKeyNotFound {
		return "", nil
	}
	return id, err
}

func (s *Store) SaveBranch(b *version.Branch) error {
	data, err := json.Marshal(b)
	if err != nil {
		return err
	}
	return s.db.Update(func(txn *badger.Txn) error {
		return txn.Set(branchKey(b.Name), data)
	})
}

func (s *Store) GetBranch(name string) (*version.Branch, error) {
	var b version.Branch
	err := s.db.View(func(txn *badger.Txn) error {
		item, err := txn.Get(branchKey(name))
		if err != nil {
			return err
		}
		return item.Value(func(val []byte) error {
			return json.Unmarshal(val, &b)
		})
	})
	if err == badger.ErrKeyNotFound {
		return nil, nil
	}
	return &b, err
}

func (s *Store) ListBranches() ([]*version.Branch, error) {
	var branches []*version.Branch
	err := s.db.View(func(txn *badger.Txn) error {
		prefix := []byte("branch:")
		it := txn.NewIterator(badger.DefaultIteratorOptions)
		defer it.Close()
		for it.Seek(prefix); it.ValidForPrefix(prefix); it.Next() {
			item := it.Item()
			var b version.Branch
			if err := item.Value(func(val []byte) error {
				return json.Unmarshal(val, &b)
			}); err != nil {
				return err
			}
			branches = append(branches, &b)
		}
		return nil
	})
	return branches, err
}

func (s *Store) SaveTag(t *version.Tag) error {
	data, err := json.Marshal(t)
	if err != nil {
		return err
	}
	return s.db.Update(func(txn *badger.Txn) error {
		return txn.Set(tagKey(t.Name), data)
	})
}

func (s *Store) GetTag(name string) (*version.Tag, error) {
	var t version.Tag
	err := s.db.View(func(txn *badger.Txn) error {
		item, err := txn.Get(tagKey(name))
		if err != nil {
			return err
		}
		return item.Value(func(val []byte) error {
			return json.Unmarshal(val, &t)
		})
	})
	if err == badger.ErrKeyNotFound {
		return nil, nil
	}
	return &t, err
}

func (s *Store) ListTags() ([]*version.Tag, error) {
	var tags []*version.Tag
	err := s.db.View(func(txn *badger.Txn) error {
		prefix := []byte("tag:")
		it := txn.NewIterator(badger.DefaultIteratorOptions)
		defer it.Close()
		for it.Seek(prefix); it.ValidForPrefix(prefix); it.Next() {
			item := it.Item()
			var t version.Tag
			if err := item.Value(func(val []byte) error {
				return json.Unmarshal(val, &t)
			}); err != nil {
				return err
			}
			tags = append(tags, &t)
		}
		return nil
	})
	return tags, err
}

func (s *Store) ListCommitsByBranch(branch string) ([]*version.Commit, error) {
	var commits []*version.Commit
	err := s.db.View(func(txn *badger.Txn) error {
		prefix := []byte("commit:")
		it := txn.NewIterator(badger.DefaultIteratorOptions)
		defer it.Close()
		for it.Seek(prefix); it.ValidForPrefix(prefix); it.Next() {
			item := it.Item()
			var c version.Commit
			if err := item.Value(func(val []byte) error {
				return json.Unmarshal(val, &c)
			}); err != nil {
				return err
			}
			if c.Branch == branch {
				commits = append(commits, &c)
			}
		}
		return nil
	})
	return commits, err
}

func (s *Store) GetCommitByCID(cid string) (*version.Commit, error) {
	var found *version.Commit
	err := s.db.View(func(txn *badger.Txn) error {
		prefix := []byte("commit:")
		it := txn.NewIterator(badger.DefaultIteratorOptions)
		defer it.Close()
		for it.Seek(prefix); it.ValidForPrefix(prefix); it.Next() {
			item := it.Item()
			var c version.Commit
			if err := item.Value(func(val []byte) error {
				return json.Unmarshal(val, &c)
			}); err != nil {
				return err
			}
			if c.CID == cid {
				found = &c
				return nil
			}
		}
		return nil
	})
	return found, err
}

func (s *Store) DeleteBranch(name string) error {
	return s.db.Update(func(txn *badger.Txn) error {
		return txn.Delete(branchKey(name))
	})
}

func (s *Store) DeleteTag(name string) error {
	return s.db.Update(func(txn *badger.Txn) error {
		return txn.Delete(tagKey(name))
	})
}
