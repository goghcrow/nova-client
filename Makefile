nova: NovaClient.c ThriftGeneric.c BinaryData.c Nova.c cJSON.c Debugger.c
	$(CC) -g -Wall -o $@ $^

clean:
	rm nova