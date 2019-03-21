#include <infiniband/verbs.h>
#include <stdio.h>

int main()
{
	struct ibv_device **ib_devices;
	struct ibv_context **ib_contexts;
	int num_devices;

	ib_devices = ibv_get_device_list(&num_devices);
	ib_contexts = (struct ibv_context **)malloc(sizeof(struct ibv_context *));

	for (int i = 0; i < num_devices; ++i)
	{
		printf("RDMA device %d: name=%s, node type=%s\n", i, ibv_get_device_name(ib_devices[i]), ibv_node_type_str(ib_devices[i]->node_type));
		ib_contexts[i] = ibv_open_device(ib_devices[i]);
		printf("Opened the device\n");
		ibv_close_device(ib_contexts[i]);
		printf("Closed the device\n");
	}

	ibv_free_device_list(ib_devices);
	free(ib_contexts);
}
