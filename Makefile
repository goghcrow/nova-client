nova: NovaClient.c ThriftGeneric.c BinaryData.c Nova.c cJSON.c
	$(CC) -g -Wall -o $@ $^ -lpthread

clean:
	rm nova