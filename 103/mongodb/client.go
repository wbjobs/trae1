package mongodb

import (
	"context"
	"fmt"

	"go.mongodb.org/mongo-driver/bson"
	"go.mongodb.org/mongo-driver/mongo"
	"go.mongodb.org/mongo-driver/mongo/options"
)

type Client struct {
	client   *mongo.Client
	database *mongo.Database
}

type ChangeEvent struct {
	OperationType string                 `bson:"operationType"`
	FullDocument  bson.M                 `bson:"fullDocument"`
	DocumentKey   bson.M                 `bson:"documentKey"`
	ResumeToken   bson.Raw               `bson:"_id"`
	Namespace     struct {
		Db         string `bson:"db"`
		Coll       string `bson:"coll"`
	} `bson:"ns"`
	UpdateDescription struct {
		UpdatedFields bson.M `bson:"updatedFields"`
		RemovedFields []string `bson:"removedFields"`
	} `bson:"updateDescription"`
}

func NewClient(uri, database string) (*Client, error) {
	ctx := context.TODO()
	client, err := mongo.Connect(ctx, options.Client().ApplyURI(uri))
	if err != nil {
		return nil, fmt.Errorf("failed to connect to MongoDB: %w", err)
	}

	if err := client.Ping(ctx, nil); err != nil {
		return nil, fmt.Errorf("failed to ping MongoDB: %w", err)
	}

	return &Client{
		client:   client,
		database: client.Database(database),
	}, nil
}

func (c *Client) Close() error {
	return c.client.Disconnect(context.TODO())
}

func (c *Client) Watch(ctx context.Context, resumeToken []byte) (*mongo.ChangeStream, error) {
	opts := options.ChangeStream()
	if resumeToken != nil {
		opts.SetResumeAfter(bson.Raw(resumeToken))
	}

	pipeline := mongo.Pipeline{
		{{"$match", bson.D{{"operationType", bson.D{{"$in", bson.A{"insert", "update", "replace", "delete"}}}}}}},
		{{"$project", bson.D{{"operationType", 1}, {"fullDocument", 1}, {"documentKey", 1}, {"_id", 1}, {"ns", 1}, {"updateDescription", 1}}}},
	}

	stream, err := c.database.Watch(ctx, pipeline, opts)
	if err != nil {
		return nil, fmt.Errorf("failed to start change stream: %w", err)
	}

	return stream, nil
}

func (c *Client) GetCollection(collection string) *mongo.Collection {
	return c.database.Collection(collection)
}

func (c *Client) FindAll(ctx context.Context, collection string) (*mongo.Cursor, error) {
	return c.database.Collection(collection).Find(ctx, bson.M{})
}
