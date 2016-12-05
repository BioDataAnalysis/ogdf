//****************:*****************************************
//  Test helpers for layout algorithms
//
//  Author: Carsten Gutwenger, Tilo Wiedera
//*********************************************************
#ifndef LAYOUT_HELPERS_H
#define LAYOUT_HELPERS_H

#include <random>
#include <bandit/bandit.h>
#include <resources.h>

#include <ogdf/basic/GraphAttributes.h>
#include <ogdf/module/LayoutModule.h>
#include <ogdf/module/GridLayoutModule.h>
#include <ogdf/basic/graph_generators.h>
#include <ogdf/basic/simple_graph_alg.h>

#define MAX_NODES 200
// FIXME: Crashes upon setting MIN_NODES to 0
//         probably related to planarBiconnectedGraph
#define MIN_NODES 25
#define STEP_SIZE 25

namespace ogdf {

enum GraphRequirementFlags {
	GR_ALL = 0,
	GR_PLANAR = 1,
	GR_TRIPLE_CONNECTED = 2,
	GR_CONNECTED = 4
};

inline void insertGraph(Graph &g, const Graph &g2)
{
	NodeArray<node> map(g2);

	for(node v : g2.nodes)
		map[v] = g.newNode();

	for(edge e: g2.edges)
		g.newEdge(map[e->source()], map[e->target()]);
}

inline void createDisconnectedGraph(
	Graph &G,
	int n_max,
	double density_min,
	double density_max,
	int cc)
{
	int n = randomNumber(3,n_max);
	int m = randomNumber((int)(density_min*n),(int)(density_max*n));
	int b = n/25+1;
	planarCNBGraph(G, n/b+1, m/b+1, b);

	for(int i = cc-1; i > 0; --i) {
		Graph g2;
		n = randomNumber(3,n);
		m = randomNumber((int)(density_min*n),(int)(density_max*n));
		b = n/25+1;
		planarCNBGraph(g2, n/b+1, m/b+1, b);
		insertGraph(G,g2);
	}
}

inline void createAlmostPlanarGraph(Graph &G, int n, int m, int add_m)
{
	planarBiconnectedGraph(G, n, m);

	Array<node> table(n);

	int i = 0;
	for(node v : G.nodes) {
		OGDF_ASSERT(i < n);
		table[i++] = v;
	}

	for(i = 0; i < add_m; ++i) {
		G.newEdge(table[randomNumber(0, n-1)], table[randomNumber(0, n-1)]);
	}

	makeSimpleUndirected(G);
}

inline void getRandomLayout(GraphAttributes &GA)
{
	const Graph &G = GA.constGraph();
	double max_x = 2.0 * sqrt(G.numberOfNodes());
	double max_y = max_x;

	std::minstd_rand rng(randomSeed());
	std::uniform_real_distribution<> rand_x(0.0,max_x);
	std::uniform_real_distribution<> rand_y(0.0,max_y);

	for(node v : G.nodes) {
		GA.x(v) = rand_x(rng);
		GA.y(v) = rand_y(rng);
	}
}

inline int64_t callLayout(const Graph &G, LayoutModule &L, bool isGridLayout, long extraAttributes)
{
	int64_t T;

	if(isGridLayout) {
		GridLayout gl;
		System::usedRealTime(T);
		((GridLayoutModule*)&L)->callGrid(G, gl);
	}
	else {
		extraAttributes |= GraphAttributes::nodeGraphics | GraphAttributes::edgeGraphics;
		GraphAttributes GA(G, extraAttributes);
		getRandomLayout(GA);
		System::usedRealTime(T);
		L.call(GA);
	}
	return System::usedRealTime(T);
}

/**
 * Runs several tests for a given layout module.
 * The layout algorithm is executed for different graphs.
 * There are no assertions yet.
 *
 * \param name
 * 	the name to be used for describing this module
 * \param L
 * 	the module to be tested
 * \param extraAttributes
 *  init attributes for GraphAttributes
 * \param req
 * 	the requirements for graphs to be drawn, see GraphRequirementFlags for details
 * \param maxNodes
 * 	the maximum number of nodes the algorithm should be tested on
 * \param isGridLayout
 * 	set this to true if the layout module is a grid layout module
 * \param skipTreeWithProbablyNegativeCoordinates
 *  set this to true if the file negative_coordinates_tree.gml should be skipped
 **/
inline void describeLayoutModule(
  const std::string name,
  LayoutModule &L,
  long extraAttributes = 0,
  int req = GR_ALL,
  int maxNodes = MAX_NODES,
  bool isGridLayout = false,
  bool skipTreeWithProbablyNegativeCoordinates = false)
{
	int steps = (maxNodes - MIN_NODES)/STEP_SIZE;

	bandit::describe(name, [&](){
		if(!(req & GR_TRIPLE_CONNECTED)) {
			bandit::it("works on trees", [&](){
				int64_t time = 0;
				int sizes[] = { 38, 50, 63 };
				for(int n = MIN_NODES; n < maxNodes; n += STEP_SIZE) {
					for(int i = 0; i < 3; i++) {
						Graph G;
						randomTree(G, n);
						time += callLayout(G, L, isGridLayout, extraAttributes);
					}
				}
				cout << endl << "      average time was " << time/steps/(sizeof(sizes)/sizeof(sizes[0])) << "ms" << endl;
			});

			if (!skipTreeWithProbablyNegativeCoordinates) {
				for_each_graph_it("works on a tree with probably negative coordinates",
								  { "misc/negative_coordinates_tree.gml" },
								  [&](Graph &G, const string &filename) {
									  callLayout(G, L, isGridLayout, extraAttributes);
								  });
			}

			bandit::it("works on planar connected graphs", [&](){
				int64_t time = 0;
				int sizes[] = { 38, 50, 63 };
				for(int n = MIN_NODES; n < maxNodes; n += STEP_SIZE) {
					for(int i = 0; i < 3; i++) {
						Graph G;
						planarCNBGraph(G, n, sizes[i], n/25);
						makeSimpleUndirected(G);
						time += callLayout(G, L, isGridLayout, extraAttributes);
					}
				}
				cout << endl << "      average time was " << time/steps/(sizeof(sizes)/sizeof(sizes[0])) << "ms" << endl;
			});

			bandit::it("works on planar biconnected graphs", [&](){
				int64_t time = 0;
				for(int n = MIN_NODES; n < maxNodes; n += STEP_SIZE) {
					Graph G;
					planarBiconnectedGraph(G, n, 3*n/2);
					time += callLayout(G, L, isGridLayout, extraAttributes);
					planarBiconnectedGraph(G, n, 2*n);
					time += callLayout(G, L, isGridLayout, extraAttributes);
					planarBiconnectedGraph(G, n, 5*n/2);
					time += callLayout(G, L, isGridLayout, extraAttributes);
				}
				cout << endl << "      average time was " << time/steps/3 << "ms" << endl;
			});
		}

		bandit::it("works on planar triconnected graphs", [&](){
			int64_t time = 0;
			int sizes[] = { 38, 50, 63 };
			for(int n = MIN_NODES; n < maxNodes; n += STEP_SIZE) {
				for(int i = 0; i < 3; i++) {
					Graph G;
					planarTriconnectedGraph(G, n, sizes[i], n/25);
					time += callLayout(G, L, isGridLayout, extraAttributes);
				}
			}
			cout << endl << "      average time was " << time/steps/(sizeof(sizes)/sizeof(sizes[0])) << "ms" << endl;
		});

		if(!(req & (GR_PLANAR | GR_TRIPLE_CONNECTED))) {
			bandit::it("works on almost planar graphs", [&](){
				int64_t time = 0;
				for(int n = MIN_NODES; n < MAX_NODES; n += STEP_SIZE) {
					Graph G;
					createAlmostPlanarGraph(G, n, 3*n/2, 10);
					time += callLayout(G, L, isGridLayout, extraAttributes);
					createAlmostPlanarGraph(G, n, 2*n, 10);
					time += callLayout(G, L, isGridLayout, extraAttributes);
					createAlmostPlanarGraph(G, n, 5*n/2, 10);
					time += callLayout(G, L, isGridLayout, extraAttributes);
				}
				cout << endl << "      average time was " << time/steps/3 << "ms" << endl;
			});

			bandit::it("works on biconnected graphs", [&](){
				int64_t time = 0;
				for(int n = MIN_NODES; n < maxNodes; n += STEP_SIZE) {
					Graph G;
					randomBiconnectedGraph(G, n, n*(n-1)/2);
					makeSimpleUndirected(G);
					time += callLayout(G, L, isGridLayout, extraAttributes);
				}
				cout << endl << "      average time was " << time/steps << "ms" << endl;
			});

			if(!(req & GR_CONNECTED)) {
				bandit::it("works on disconnected graphs", [&](){
					int64_t time = 0;
					for(int n = MIN_NODES; n < maxNodes; n += STEP_SIZE) {
						Graph G;
						createDisconnectedGraph(G, n/7, 1.4, 2.6, 7);
						time += callLayout(G, L, isGridLayout, extraAttributes);
					}
					cout << endl << "      average time was " << time/steps << "ms" << endl;
				});
			}
		}
	});
}

/**
 * Runs several tests for a given layout module.
 * The layout algorithm is executed for different graphs.
 * There are no assertions yet.
 *
 * \param name
 * 	the name to be used for describing this module
 * \param L
 * 	the module to be tested
 * \param req
 * 	the requirements for graphs to be drawn, see GraphRequirementFlags for details
 * \param maxNodes
 * 	the maximum number of nodes the algorithm should be tested on
 * \param isGridLayout
 * 	set this to true if the layout module is a grid layout module
 **/
inline void describeGridLayoutModule(
  const std::string name,
  GridLayoutModule &L,
  int req = GR_ALL,
  int maxNodes = MAX_NODES)
{
	describeLayoutModule(name, L, 0, req, maxNodes, true);
}

}

#endif
