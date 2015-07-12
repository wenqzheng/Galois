/*
 * pagerank_kernel.cl
 * OpenCL kernels for PageRank on heterogeneous Galois.
 *
 *  Created on: Jun 30, 2015
 *      Author: rashid
 */

#define alpha (1.0 - 0.85)

typedef uint EdgeData;
typedef uint NodeData;
//#include "graph_header.h"
//##########################################
//Graph header, created ::Tue Jun  2 16:02:26 2015
typedef struct _GraphType {
uint _num_nodes;
uint _num_edges;
 uint _node_data_size;
 uint _edge_data_size;
 __global NodeData *_node_data;
 __global uint *_out_index;
 __global uint *_out_neighbors;
 __global EdgeData *_out_edge_data;
 }GraphType;
uint in_neighbors_begin(__local GraphType * graph, uint node){
 return 0;
}
uint in_neighbors_end(__local GraphType * graph, uint node){
 return graph->_out_index[node+1]-graph->_out_index[node];
}
uint in_neighbors_next(__local GraphType * graph, uint node){
 return 1;
}
uint in_neighbors(__local GraphType * graph, uint node, uint nbr){
 return graph->_out_neighbors[graph->_out_index[node]+nbr];
}
__global EdgeData * in_edge_data(__local GraphType * graph, uint node, uint nbr){
 return &graph->_out_edge_data[graph->_out_index[node]+nbr];
}
uint out_neighbors_begin(__local GraphType * graph, uint node){
 return 0;
}
uint out_neighbors_end(__local GraphType * graph, uint node){
 return graph->_out_index[node+1]-graph->_out_index[node];
}
uint out_neighbors_next(__local GraphType * graph, uint node){
 return 1;
}
uint out_neighbors(__local GraphType * graph,uint node,  uint nbr){
 return graph->_out_neighbors[graph->_out_index[node]+nbr];
}
__global EdgeData * out_edge_data(__local GraphType * graph,uint node,  uint nbr){
 return &graph->_out_edge_data[graph->_out_index[node]+nbr];
}
__global NodeData * node_data(__local GraphType * graph, uint node){
 return &graph->_node_data[node];
}
void initialize(__local GraphType * graph, __global uint *mem_pool){
uint offset =4;
graph->_num_nodes=mem_pool[0];
graph->_num_edges=mem_pool[1];
graph->_node_data_size=mem_pool[2];
graph->_edge_data_size=mem_pool[3];
graph->_node_data= (__global NodeData *)&mem_pool[offset];
offset +=graph->_num_nodes* graph->_node_data_size;
graph->_out_index=&mem_pool[offset];
offset +=graph->_num_nodes + 1;
graph->_out_neighbors=&mem_pool[offset];
offset +=graph->_num_edges;
graph->_out_edge_data=(__global EdgeData*)&mem_pool[offset];
offset +=graph->_num_edges*graph->_edge_data_size;
}

//##########################################
__kernel void sssp(__global int * graph_ptr,__global uint* aux,  int num_items) {
   int my_id = get_global_id(0);
   __local GraphType graph;
   initialize(&graph, graph_ptr);
   if(my_id < num_items) {
      __global NodeData * sdata = node_data(&graph, my_id);
      NodeData min_dist=  *sdata;
      for(int i= out_neighbors_begin(&graph, my_id); i<out_neighbors_end(&graph, my_id); ++i) {
         int dst_id = out_neighbors(&graph, my_id, i);
         NodeData ddata = *node_data(&graph, dst_id);
         EdgeData ewt = *out_edge_data(&graph, my_id, i);
         min_dist = min(min_dist, ewt+ddata);
      }//end for
      aux[my_id] = min_dist;
   }//end if
}//end kernel

__kernel void writeback(__global int * graph_ptr,__global uint* aux,  int num_items) {
   int my_id = get_global_id(0);
   __local GraphType graph;
   initialize(&graph, graph_ptr);
   if(my_id < num_items) {
      __global NodeData * sdata = node_data(&graph, my_id);
      *sdata = aux[my_id];
   }//end if
}//end kernel

__kernel void initialize_nodes(__global int * graph_ptr,__global uint* aux,  int num_items) {
   int my_id = get_global_id(0);
   __local GraphType graph;
   initialize(&graph, graph_ptr);
   if(my_id < graph._num_nodes){ // num_items) {
      *node_data(&graph, my_id)=INT_MAX/2;
   }//end if
}//end kernel
