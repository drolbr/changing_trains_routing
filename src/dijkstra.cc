#include "read_input.h"
#include "geocode.h"


#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>


struct Routing_Node;

struct Routing_Edge
{
  Routing_Edge() : start(0), end(0), valuation(0) {}
  
  std::vector< Coord > trace;
  double valuation;
  const Routing_Node* start;
  const Routing_Node* end;
};


struct Routing_Node
{
  Routing_Node(Id_Type id_) : id(id_) {}
  
  Id_Type id;
  std::vector< const Routing_Edge* > edges;
  
  bool operator<(const Routing_Node& rhs) const { return id < rhs.id; }
};


class Routing_Data
{
public:
  Routing_Data(const Parsing_State& data);
  
  void print_statistics() const;
  
  std::pair< const Routing_Edge*, unsigned int > resolve_way_pos(unsigned int way_id, unsigned int index) const;
  
private:
  std::vector< Routing_Node > nodes;
  std::vector< Routing_Edge > edges;
  
  std::vector< std::pair< Id_Type, std::vector< std::pair< unsigned int, unsigned int > > > > way_dictionary;
  
  Routing_Edge edge_from_way(const Way& way, unsigned int start, unsigned int end, const Parsing_State& data) const;
};


struct Route_Ref
{
  Route_Ref(const Routing_Data& routing_data, const Way_Reference& way_ref, const std::string label,
	    const Parsing_State& data);
  
  double proportionate_valuation() const;
  
  std::string label;
  const Routing_Edge* edge;
  unsigned int index;
  double pos;
};


struct Route
{
  Route(const Route_Ref& start_, const Route_Ref& end_, double value_) : start(start_), end(end_), value(value_) {}
  
  Route_Ref start;
  Route_Ref end;
  std::vector< const Routing_Edge* > edges;
  double value;
};


struct Route_Tree
{
  Route_Tree(const Routing_Data& routing_data, const Route_Ref& origin, const std::vector< Route_Ref >& destinations);
  
  std::vector< Route > routes;
};


double distance(const Coord& a, const Coord& b)
{
  static double PI = acos(0)*2;
  
  double lon_scale = 1.0/cos((a.lat + b.lat) / 2.0 * PI / 180.0);
  return sqrt((b.lat - a.lat) * (b.lat - a.lat)
      + (b.lon - a.lon) * lon_scale * (b.lon - a.lon) * lon_scale);
}


Routing_Edge Routing_Data::edge_from_way(const Way& way, unsigned int start, unsigned int end,
					 const Parsing_State& data) const
{
  Routing_Edge edge;
  std::vector< Routing_Node >::const_iterator n_it
      = std::lower_bound(nodes.begin(), nodes.end(), Routing_Node(way.nds[start]));
  if (n_it != nodes.end())
    edge.start = &(*n_it);
  n_it = std::lower_bound(nodes.begin(), nodes.end(), Routing_Node(way.nds[end]));
  if (n_it != nodes.end())
    edge.end = &(*n_it);

  Coord last_coord(0, 0);
  for (unsigned int i = start; i <= end; ++i)
  {
    std::vector< Node >::const_iterator n_it =
        std::lower_bound(data.nodes.begin(), data.nodes.end(), Node(way.nds[i]));
    if (n_it != data.nodes.end() && n_it->id == way.nds[i])
    {
      Coord coord(n_it->lat, n_it->lon);
      edge.trace.push_back(coord);
      if (i > start)
	edge.valuation += distance(last_coord, coord);
      last_coord = coord;
    }
  }
  
  return edge;
}


Routing_Data::Routing_Data(const Parsing_State& data)
{
  std::map< Id_Type, unsigned int > node_count;
  
  for (std::vector< Way >::const_iterator it = data.ways.begin(); it != data.ways.end(); ++it)
  {
    if (!it->nds.empty())
    {
      node_count[it->nds[0]] += 2;
      for (unsigned int i = 1; i < it->nds.size()-1; ++i)
        ++node_count[it->nds[i]];
      node_count[it->nds[it->nds.size()-1]] += 2;
    }
  }
  
  for (std::map< Id_Type, unsigned int >::const_iterator it = node_count.begin(); it != node_count.end(); ++it)
  {
    if (it->second > 1)
      nodes.push_back(Routing_Node(it->first));
  }
  
  for (std::vector< Way >::const_iterator it = data.ways.begin(); it != data.ways.end(); ++it)
  {
    unsigned int start = 0;
    way_dictionary.push_back(std::make_pair(Id_Type(it->id),
        std::vector< std::pair< unsigned int, unsigned int > >()));
    std::vector< std::pair< unsigned int, unsigned int > >& dictionary_entry = way_dictionary.back().second;
    for (unsigned int i = 1; i < it->nds.size(); ++i)
    {
      if (node_count[it->nds[i]] > 1)
      {
	dictionary_entry.push_back(std::make_pair(start, edges.size()));
	edges.push_back(edge_from_way(*it, start, i, data));
	start = i;
      }
    }
    if (start < it->nds.size() - 1)
    {
      dictionary_entry.push_back(std::make_pair(start, edges.size()));
      edges.push_back(edge_from_way(*it, start, it->nds.size() - 1, data));
    }
  }
  
  for (std::vector< Routing_Edge >::const_iterator it = edges.begin(); it != edges.end(); ++it)
  {
    std::vector< Routing_Node >::iterator n_it
	= std::lower_bound(nodes.begin(), nodes.end(), *it->start);
    if (n_it != nodes.end())
      n_it->edges.push_back(&(*it));
    n_it = std::lower_bound(nodes.begin(), nodes.end(), *it->end);
    if (n_it != nodes.end())
      n_it->edges.push_back(&(*it));
  }
}


void Routing_Data::print_statistics() const
{
  double total_valuation = 0;
  for (std::vector< Routing_Edge >::const_iterator it = edges.begin(); it != edges.end(); ++it)
    total_valuation += it->valuation;
  
  std::cout<<nodes.size()<<' '<<edges.size()<<' '<<total_valuation * 111111.1<<'\n';
}


template< typename First, typename Second >
struct Compare_By_First
{
  bool operator()(const std::pair< First, Second >& lhs, const std::pair< First, Second >& rhs) const
  { return lhs.first < rhs.first; }
};


std::pair< const Routing_Edge*, unsigned int > Routing_Data::resolve_way_pos
    (unsigned int way_id, unsigned int index) const
{
  std::vector< std::pair< Id_Type, std::vector< std::pair< unsigned int, unsigned int > > > >::const_iterator w_it
      = std::lower_bound(way_dictionary.begin(), way_dictionary.end(),
      std::make_pair(Id_Type(way_id), std::vector< std::pair< unsigned int, unsigned int > >()),
      Compare_By_First< Id_Type, std::vector< std::pair< unsigned int, unsigned int > > >());
  if (w_it != way_dictionary.end())
  {
    std::vector< std::pair< unsigned int, unsigned int > >::const_iterator entry_it = w_it->second.begin();
    while (entry_it != w_it->second.end() && entry_it->first <= index)
      ++entry_it;
    --entry_it;
    return std::make_pair(&edges[entry_it->second], index - entry_it->first);
  }
  else
    return std::pair< const Routing_Edge*, unsigned int >(0, 0);
}


Route_Ref::Route_Ref(const Routing_Data& routing_data, const Way_Reference& way_ref, const std::string label_,
		     const Parsing_State& data)
  : label(label_), edge(0), index(0), pos(way_ref.pos)
{
  std::pair< const Routing_Edge*, unsigned int > edge_index
      = routing_data.resolve_way_pos(way_ref.way_ref, way_ref.index);
  edge = edge_index.first;
  index = edge_index.second;
}


double Route_Ref::proportionate_valuation() const
{
  if (!edge)
    return 0;
  
  double total_length = 0;
  double partial_length = 0;
  
  for (unsigned int i = 1; i < edge->trace.size(); ++i)
  {
    double distance_ = distance(edge->trace[i-1], edge->trace[i]);
    if (i == index+1)
      partial_length = total_length + pos;
    total_length += distance_;
  }
  
  if (total_length == 0)
    return edge->valuation;
  else
    return edge->valuation * partial_length / total_length;
}


struct Open_Node
{
  Open_Node(const Routing_Node* node_, const Routing_Edge* arrived_from_, double value_)
      : node(node_), arrived_from(arrived_from_), value(value_) {}
  
  const Routing_Node* node;
  const Routing_Edge* arrived_from;
  double value;
  
  // Sorts in descending order of current value.
  // This allows to pop back the closest value in the end.
  bool operator<(const Open_Node& rhs) const { return rhs.value < value; }
};


struct Closed_Node
{
  Closed_Node(const Routing_Edge* arrived_from_, double value_)
      : arrived_from(arrived_from_), value(value_) {}
  
  const Routing_Edge* arrived_from;
  double value;
};


void eval_edge_for_destinations(const Routing_Edge& edge, const std::vector< Route_Ref >& destinations,
    const Route_Ref& origin, double start_value, double end_value, std::vector< Route >& routes)
{
  for (std::vector< Route_Ref >::const_iterator it = destinations.begin(); it != destinations.end(); ++it)
  {
    if (it->edge == &edge && routes[std::distance(destinations.begin(), it)].value == 180.0)
    {
      Route route(origin, *it, 0);
      
      double proportionate_valuation_ = it->proportionate_valuation();
      if (start_value + proportionate_valuation_ < end_value + edge.valuation - proportionate_valuation_)
      {
	route.value = start_value + proportionate_valuation_;
	//route.edges.swap(resolve_edges(...));
      }
      else
      {
	route.value = end_value + edge.valuation - proportionate_valuation_;
	//route.edges.swap(resolve_edges(...));
      }
      route.edges.push_back(&edge);
      routes[std::distance(destinations.begin(), it)] = route;
    }
  }
}


Route_Tree::Route_Tree
    (const Routing_Data& routing_data, const Route_Ref& origin, const std::vector< Route_Ref >& destinations)
{
  for (std::vector< Route_Ref >::const_iterator it = destinations.begin(); it != destinations.end(); ++it)
  {
    if (it->edge == origin.edge && it->index == origin.index)
      routes.push_back(Route(origin, *it, fabs(it->pos - origin.pos)));
    else
      routes.push_back(Route(origin, *it, 180.0));
  }
  
  std::map< const Routing_Node*, Closed_Node > final_tree;
  std::vector< Open_Node > open_nodes;
  open_nodes.push_back(Open_Node(origin.edge->start, origin.edge, origin.proportionate_valuation()));
  open_nodes.push_back(Open_Node(origin.edge->end, origin.edge,
      origin.edge->valuation - origin.proportionate_valuation()));
  
  while (!open_nodes.empty())
  {
    std::sort(open_nodes.begin(), open_nodes.end());
    Open_Node current = open_nodes.back();
    open_nodes.pop_back();
    
    if (final_tree.find(current.node) != final_tree.end())
      continue;
    
//     std::cout<<current.node->id<<' '<<current.value * 111111.111<<' '<<open_nodes.size()<<'\n';
    
    final_tree.insert(std::make_pair(current.node, Closed_Node(current.arrived_from, current.value)));
    
    for (std::vector< const Routing_Edge* >::const_iterator it = current.node->edges.begin();
	 it != current.node->edges.end(); ++it)
    {
      if ((*it)->start == current.node)
      {
	std::map< const Routing_Node*, Closed_Node >::const_iterator f_it = final_tree.find((*it)->end);
	if (f_it == final_tree.end())
	  open_nodes.push_back(Open_Node((*it)->end, *it, current.value + (*it)->valuation));
	else
	  eval_edge_for_destinations(**it, destinations, origin, current.value, f_it->second.value, routes);
      }
      else if ((*it)->end == current.node)
      {
	std::map< const Routing_Node*, Closed_Node >::const_iterator f_it = final_tree.find((*it)->start);
	if (f_it == final_tree.end())
	  open_nodes.push_back(Open_Node((*it)->start, *it, current.value + (*it)->valuation));
	else
	  eval_edge_for_destinations(**it, destinations, origin, f_it->second.value, current.value, routes);
      }
    }
  }
}


int main(int argc, char* args[])
{
  const Parsing_State& state = read_osm();
  
  Routing_Data routing_data(state);
  routing_data.print_statistics();
  
  std::vector< Route_Ref > destinations;
  
  for (std::vector< Way >::const_iterator it = state.ways.begin(); it != state.ways.end(); ++it)
  {
    if (has_kv(*it, "railway", "platform") ||
        (has_kv(*it, "public_transport", "platform") && !has_kv(*it, "bus", "yes")))
    {
      std::cout<<"way "<<it->id<<'\n';
      Geometry geom(*it, state);
      Coord center = geom.bbox_center();
//       std::cout<<"  "<<center.lat<<' '<<center.lon<<'\n';
      std::string label = "-";
      unsigned int label_level = 0;
      for (std::vector< std::pair< std::string, std::string > >::const_iterator it2 = it->tags.begin();
          it2 != it->tags.end(); ++it2)
      {
// 	std::cout<<"  "<<it2->first<<" = "<<it2->second<<'\n';
	if (it2->first == "local_ref")
	{
	  label = it2->second;
	  label_level = 3;
	}
	else if (label_level < 2 && it2->first == "ref")
	{
	  label = it2->second;
	  label_level = 2;
	}
	else if (label_level < 1 && it2->first == "name")
	{
	  label = it2->second;
	  label_level = 1;
	}
      }
      Way_Reference ref(*it, center, state);
//       std::cout<<"  "<<ref.way_ref<<' '<<ref.index<<' '<<ref.pos * 111111.1<<'\n';
      destinations.push_back(Route_Ref(routing_data, ref, label, state));
    }
  }
  
  for (std::vector< Relation >::const_iterator it = state.relations.begin(); it != state.relations.end(); ++it)
  {
    if (has_kv(*it, "railway", "platform") ||
        (has_kv(*it, "public_transport", "platform") && !has_kv(*it, "bus", "yes")))
    {
      std::cout<<"relation "<<it->id<<'\n';
      Geometry geom(*it, state);
      Coord center = geom.bbox_center();
//       std::cout<<"  "<<center.lat<<' '<<center.lon<<'\n';
      std::string label = "-";
      unsigned int label_level = 0;
      for (std::vector< std::pair< std::string, std::string > >::const_iterator it2 = it->tags.begin();
          it2 != it->tags.end(); ++it2)
      {
// 	std::cout<<"  "<<it2->first<<" = "<<it2->second<<'\n';
	if (it2->first == "local_ref")
	{
	  label = it2->second;
	  label_level = 3;
	}
	else if (label_level < 2 && it2->first == "ref")
	{
	  label = it2->second;
	  label_level = 2;
	}
	else if (label_level < 1 && it2->first == "name")
	{
	  label = it2->second;
	  label_level = 1;
	}
      }
      Way_Reference ref(*it, center, state);
//       std::cout<<"  "<<ref.way_ref<<' '<<ref.index<<' '<<ref.pos * 111111.1<<'\n';
      destinations.push_back(Route_Ref(routing_data, ref, label, state));
    }
  }
  
  for (std::vector< Route_Ref >::const_iterator it = destinations.begin(); it != destinations.end(); ++it)
  {
    Route_Tree tree(routing_data, *it, destinations);
    
    for (std::vector< Route >::const_iterator r_it = tree.routes.begin(); r_it != tree.routes.end(); ++r_it)
      std::cout<<r_it->value * 111111.1<<'\t'<<r_it->start.label<<'\t'<<r_it->end.label<<'\n';
  }
  
  return 0;
}
