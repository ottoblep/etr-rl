//
// Created by Linus on 19.03.2022.
//


#include "rayNEAT.h"

Network::Network(Neat_Instance *neatInstance) : neat_instance(neatInstance), fitness(0) {
    //init input- and output nodes
    for (int i = 0; i < neat_instance->input_count + neat_instance->output_count; i++) {
        nodes[i] = {neat_instance->request_node_gene(node_id(i)), 0.f};
    }
    //init a random connection for every input
    for (int i = 0; i < neat_instance->input_count; i++) {
        /*for (int j = input_count; j < input_count + output_count; j++) {
            addConnection(neatInstance, i, j, 1.f);
        }*/
        add_connection(i, rand() % neat_instance->output_count + neat_instance->input_count, 1.f);
    }
}

Network::Network(Neat_Instance *neatInstance, const string &line) : neat_instance(neatInstance) {
    vector<string> datapoints = split(line, "||");
    //fitness
    fitness = datapoints.empty() ? 0.f : safe_to_float(datapoints[0], 0.f);
    //nodes
    if (datapoints.size() > 1) {
        vector<string> node_data = split(datapoints[1], ";");
        for (string &nd: node_data) {
            if (nd.empty()) continue;
            auto id = node_id(safe_to_int(nd, 0));
            nodes[id] = {neat_instance->request_node_gene(id), 0.f};
        }
    }
    //connections
    if (datapoints.size() > 2) {
        vector<string> connection_data = split(datapoints[2], ";");
        for (string &cd: connection_data) {
            if (cd.empty()) continue;
            vector<string> connection_datapoints = split(cd, "|");
            if (connection_datapoints.size() < 3) continue;
            //search gene with fitting id and add it
            connection_id id = safe_to_int(connection_datapoints[0], 0);
            connections[id] = {
                    neat_instance->request_connection_gene(id),
                    safe_to_int(connection_datapoints[1], 0) != 0,
                    safe_to_float(connection_datapoints[2], 0.f)
            };
        }
    }
}


void Network::mutate() {
    if (rnd_f(0.f, 1.f) < neat_instance->probability_mutate_weight) {
        mutate_weights();
    }
    if (rnd_f(0.f, 1.f) < neat_instance->probability_mutate_node) {
        mutate_addnode();
    }
    if (rnd_f(0.f, 1.f) < neat_instance->probability_mutate_link) {
        mutate_addconnection();
    }
}


void Network::mutate_weights() {
    for (auto &[id, c]: connections) {
        if (rnd_f(0.f, 1.f) < neat_instance->probability_mutate_weight_pertube) {
            //slightly pertube weight
            c.weight = std::clamp(c.weight + rnd_f(-neat_instance->mutate_weight_pertube_strength,
                                                   neat_instance->mutate_weight_pertube_strength), -2.f, 2.f);
        } else {
            //completely randomize weight
            c.weight = rnd_f(-2.f, 2.f);
        }
    }
}


void Network::mutate_addnode() {
    //select a random connection to split with a node
    int split_index = rand() % connections.size();
    connection_id split_id = std::next(connections.begin(), split_index)->first;
    Connection split = connections[split_id];
    //get new node id and inform the instance that a new node has been added
    Node_Gene new_node_gene = neat_instance->request_node_gene(split.gene);
    //add that new node to this network
    nodes[new_node_gene.id] = {new_node_gene, 0.f};
    //disable the old connection
    connections[split_id].enabled = false;
    //add new connections from old start to new node to old end
    add_connection(split.gene.start, new_node_gene.id, split.weight);
    add_connection(new_node_gene.id, split.gene.end, 1.f);
}


void Network::mutate_addconnection() {
    //create a list of all used node_ids
    list<node_id> start_candidates;
    std::transform(nodes.begin(), nodes.end(), std::back_inserter(start_candidates),
                   [](auto &pair) { return pair.first; });
    //a new connection may not start at an output node
    start_candidates.remove_if([&](node_id n) {
        return n >= neat_instance->input_count && n < neat_instance->input_count + neat_instance->output_count;
    });

    list<node_id> end_candidates;
    std::transform(nodes.begin(), nodes.end(), std::back_inserter(end_candidates),
                   [](auto &pair) { return pair.first; });
    //a new connection may not end at an input node
    end_candidates.remove_if([&](node_id n) { return n < neat_instance->input_count; });

    bool found = false;
    node_id start;
    node_id end;

    //search for a non-existing connection
    while (!found && !start_candidates.empty()) {
        //randomly select a start node from the remaining candidates
        start = *std::next(start_candidates.begin(), rand() % start_candidates.size());
        //create a copy of the end_candidates list.
        auto end_candidates_temp = end_candidates;
        //remove self
        end_candidates_temp.remove(start);
        end_candidates_temp.remove_if([&](const node_id id){return nodes[id].gene.x < nodes[start].gene.x;});
        //remove all nodes that are already the end of a connection starting at start
        for (auto &[id, c]: connections) {
            if (c.gene.start == start) {
                end_candidates_temp.remove(c.gene.end);
            }
        }
        //if there are candidates remaining, select a random one. Otherwise, remove that start node from the list and begin anew
        if (!end_candidates_temp.empty()) {
            found = true;
            end = *std::next(end_candidates_temp.begin(), rand() % end_candidates_temp.size());
        } else {
            start_candidates.remove(start);
        }
    }
    //if a connection was possible, add it. Otherwise, this network is already fully connected
    if (found) {
        add_connection(start, end, rnd_f(-2.f, 2.f));
    }
}

void Network::add_connection(node_id start, node_id end, float weight) {
    //request a connection_gene from the connection archives
    Connection_Gene cg = neat_instance->request_connection_gene(start, end);
    //add an enabled connection with that gene and the requested weight to the local map
    connections[cg.id] = {cg, true, weight};
    //make sure the start & end node of this connection are known
    nodes.try_emplace(start, Node{neat_instance->request_node_gene(start), 0.f});
    nodes.try_emplace(end, Node{neat_instance->request_node_gene(end), 0.f});
}


void Network::add_inherited_connection(Connection c) {
    connections[c.gene.id] = c;
    //make sure the nodes are registered
    nodes.try_emplace(c.gene.start, Node{neat_instance->request_node_gene(c.gene.start), 0.f});
    nodes.try_emplace(c.gene.end, Node{neat_instance->request_node_gene(c.gene.end), 0.f});
}

Network Network::reproduce(Network mother, Network father) {
    Network child(mother.neat_instance);
    child.connections.clear();

    auto m_it = mother.connections.begin();
    auto f_it = father.connections.begin();

    auto m_end = mother.connections.end();
    auto f_end = father.connections.end();
    while (m_it != m_end || f_it != f_end) {
        // ->first yiels id
        // ->seconds yields connection with that id

        if (m_it != m_end && f_it != f_end &&
            m_it->first == f_it->first) {

            //both parents contain the gene
            Connection newConnection = GetRandomValue(0, 1) == 0 ? m_it->second : f_it->second;
            //if both parents have the gene enabled, the child has it enabled. Otherwise, it is disabled with a 75% chance
            newConnection.enabled =
                    m_it->second.enabled && f_it->second.enabled || (GetRandomValue(1, 100) > 75);
            child.add_inherited_connection(newConnection);
            m_it++;
            f_it++;

        } else if (f_it == f_end ||
                   (m_it != m_end
                    && m_it->first < f_it->first)) {
            //father has iterated to the end, so we are in mothers excess genes
            //OR mother is still viable, father is still viable (we have not short-circuited the first expression) and mother has the lower innovation number
            // -> we are in mothers disjoint genes

            //excess genes in mother OR disjoint gens in mother are taken if mother fitness >= father fitness
            if (mother.fitness >= father.fitness) {
                child.add_inherited_connection(m_it->second);
            }
            m_it++;

        } else if (m_it == m_end ||
                   (f_it != f_end &&
                    m_it->first > f_it->first)) {

            //excess genes in father OR disjoint genes in father are taken if father fitness >= mother fitness
            if (mother.fitness <= father.fitness) {
                child.add_inherited_connection(f_it->second);
            }
            f_it++;

        }

    }

    return child;
}

float Network::get_compatibility_distance(Network a, Network b) {
    float N = float(std::max(a.connections.size(), b.connections.size()));
    //if (N < 20) N = 1; //Stanley suggests this, and a threshhold of 3.0. -> but then once a genome reaches size 20 it is considered similar to everything else, reducing species size to 1
    float E = 0; //number of ecess genes
    float D = 0; //number of disjoint genes
    float M = 0; //number of matching genes
    float W = 0; //sum of weight differences of matching genes


    auto a_it = a.connections.begin();
    auto b_it = b.connections.begin();

    auto a_end = a.connections.end();
    auto b_end = b.connections.end();
    while (a_it != a_end || b_it != b_end) {

        if (a_it != a_end && b_it != b_end &&
            a_it->first == b_it->first) {
            //a matching gene has been found
            M++;
            W += abs(a_it->second.weight - b_it->second.weight);
            a_it++;
            b_it++;

        } else if (b_it == b_end) {
            //excess gene of a
            E++;
            a_it++;
        } else if (a_it != a_end && a_it->first < b_it->first) {
            //disjoint gene of a
            D++;
            a_it++;
        } else if (a_it == a_end) {
            //excess gene of b
            E++;
            b_it++;
        } else if (b_it != b_end && b_it->first < a_it->first) {
            //disjoint gene of b
            D++;
            b_it++;
        }
    }

    if (N == 0) N = 1;
    if (M == 0) M = 1;
    return a.neat_instance->c1 * E / N + a.neat_instance->c2 * D / N + a.neat_instance->c3 * W / M;
}

void Network::calculate_node_value(node_id node) {
    //calculate the weighted sum of predecessor nodes and apply activation function
    float weight_sum = 0.f;
    int i = 0;
    for (const auto &[id, c]: connections) {
        if (c.gene.end == node && c.enabled) {
            weight_sum += nodes[c.gene.start].value * c.weight;
            i++;
        }
    }
    nodes[node].value = neat_instance->activation_function(weight_sum);
}

vector<float> Network::calculate(vector<float> inputs) {

    //step 1: sort nodes by x-position
    list<Node_Gene> node_order;
    std::transform(nodes.begin(), nodes.end(), std::back_inserter(node_order), [](const auto &pair){return pair.second.gene;});
    node_order.sort([](const Node_Gene &ng1, const Node_Gene &ng2){return ng1.x  < ng2.x;});

    //step 2: set inputs & propagate values through the network
    for (auto node: node_order) {
        if(node.id < neat_instance->input_count){
            nodes[node.id].value = node.id < inputs.size() ? inputs[node.id] : 0.f;
        }else{
            calculate_node_value(node.id);
        }
    }

    //step 3: add values of output nodes to result vector
    vector<float> res;
    for (int i = neat_instance->input_count; i < neat_instance->input_count + neat_instance->output_count; i++) {
        res.push_back(nodes[i].value);
    }

    return res;
}

float Network::getFitness() const {
    return fitness;
}

void Network::setFitness(float fitness_s) {
    Network::fitness = fitness_s;
}

const map<connection_id, Connection> &Network::getConnections() const {
    return connections;
}

const map<node_id, Node> &Network::getNodes() const {
    return nodes;
}

string Network::to_string() const {
    std::ostringstream res;
    res << fitness;
    res << "||";
    for (auto &[id, node]: nodes) {
        res << id << ";";
    }
    res << "||";
    for (const auto &[id, c]: connections) {
        res << c.gene.id << "|" << c.enabled << "|" << c.weight << ";";
    }
    return res.str();
}

void Network::print() const {
    std::cout << "Nodes: ";
    for (auto &[id, node]: nodes) {
        std::cout << id << "  ";
    }
    std::cout << "\nConnections:\n";
    for (const auto &[id, c]: connections) {
        std::cout << "\t" << (c.enabled ? "" : "[");
        std::cout << c.gene.id << ": " << c.gene.start << " -> " << c.gene.end << ": " << c.weight;
        std::cout << (c.enabled ? "" : "]") << "\n";
    }
}
