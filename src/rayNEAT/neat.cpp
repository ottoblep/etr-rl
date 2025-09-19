//
// Created by Linus on 20.03.2022.
//


#include "rayNEAT.h"


Neat_Instance::Neat_Instance(unsigned short input_count, unsigned short output_count, unsigned int population)
        : input_count(input_count), output_count(output_count), population(population),
          generation_number(0),
          folderpath() {
    srand(time(NULL));

    //initialization:
    //prepare node list
    for (int i = 0; i < input_count; i++) {
        node_genes.push_back({node_id(i), 0.f, float(i + 1) / float(input_count + 1), true});
    }
    for (int i = 0; i < output_count; i++) {
        node_genes.push_back({node_id(i + input_count), 1.f, float(i + 1) / float(output_count + 1), true});
    }
    //prepare network list
    networks.reserve(population);
    for (int i = 0; i < population; i++) {
        networks.emplace_back(this);
    }
}


Neat_Instance::Neat_Instance(const string &file) {
    std::ifstream savefile;
    savefile.open(file);
    string line;
    //parameters
    std::getline(savefile, line);
    input_count = std::stoi(line);
    std::getline(savefile, line);
    output_count = std::stoi(line);
    std::getline(savefile, line);
    repetitions = std::stoi(line);
    std::getline(savefile, line);
    generation_number = std::stoi(line);
    std::getline(savefile, line);
    generation_target = std::stoi(line);
    std::getline(savefile, line);
    population = std::stoi(line);
    std::getline(savefile, line);
    speciation_threshold = std::stof(line);
    //load node data
    std::getline(savefile, line);
    vector<string> node_data = split(line, ";");
    for (string &nd: node_data) {
        vector<string> datapoints = split(nd, "|");
        node_genes.push_back(
                {
                        static_cast<node_id>(std::stoi(datapoints[0])),
                        std::stof(datapoints[1]),
                        std::stof(datapoints[2]),
                        true
                });
    }
    //load connection data
    std::getline(savefile, line);
    vector<string> connection_data = split(line, ";");
    for (string &cd: connection_data) {
        vector<string> datapoints = split(cd, "|");
        connection_genes.insert(
                {
                        static_cast<connection_id>(std::stoi(datapoints[0])),
                        static_cast<connection_id>(std::stoi(datapoints[1])),
                        static_cast<connection_id>(std::stoi(datapoints[2]))
                });
    }

    networks.reserve(population);
    for (int i = 0; i < population; i++) {
        std::getline(savefile, line);
        networks.emplace_back(this, line);
    }

    savefile.close();

}

void Neat_Instance::run_neat(float (*evalNetwork)(Network)) {
    run_neat_helper([evalNetwork, this]() {

        // Use single threaded version when thread_count is 1
        if (thread_count == 1) {
            for (Network &n: networks) {
                float sum = 0;
                for (int i = 0; i < repetitions; i++) {
                    sum += evalNetwork(n);
                }
                n.setFitness(std::max(sum / float(repetitions), std::numeric_limits<float>::min()));
            }
        } else {
            // Multi-threaded version with fixed work distribution
            vector<std::thread> threads;

            for (int i = 0; i < thread_count; i++) {
                size_t start = networks.size() / thread_count * i;
                size_t end = (i == thread_count - 1) ? networks.size() : networks.size() / thread_count * (i + 1);

                threads.emplace_back([&, start, end]() {
                    for(size_t i = start; i < end; i++){
                        float sum = 0;
                        for (int j = 0; j < repetitions; j++) {
                            sum += evalNetwork(networks[i]);
                        }
                        networks[i].setFitness(std::max(sum / float(repetitions), std::numeric_limits<float>::min()));
                    }
                });
            }

            for (int i = 0; i < thread_count; i++) {
                threads[i].join();
            }
        }
    });
}


void Neat_Instance::run_neat(pair<float, float> (*compete_networks)(Network, Network)) {
    run_neat_helper([compete_networks, this]() {
        vector<float> sums;
        sums.resize(networks.size());
        for (int i = 0; i < networks.size(); i++) {
            for (int j = i + 1; j < networks.size(); j++) {
                for (int r = 0; r < repetitions; r++) {
                    auto [n1_fitness, n2_fitness] = compete_networks(networks[i], networks[j]);
                    auto [n1_fitnessb, n2_fitnessb] = compete_networks(networks[j], networks[i]);
                    sums[i] += n1_fitness + n1_fitnessb;
                    sums[j] += n2_fitness + n2_fitnessb;
                }
            }
            //this network has fought all other networks (except itself), so we can now calculate its total fitness
            networks[i].setFitness(std::max(sums[i] / float(repetitions), std::numeric_limits<float>::min()));
        }
    });
}

void Neat_Instance::assign_networks_to_species() {
    //clear old species list
    for (Species &s: species) {
        s.networks.clear();
    }

    //assign each species to a network
    for (Network &network: networks) {

        //first, search for a species this network fits in
        auto fitting_species = std::find_if(
                species.begin(), species.end(),
                [&](const Species &s) {
                    return Network::get_compatibility_distance(s.representative, network) < speciation_threshold;
                }
        );

        //if no fitting species was found, create new one with this network as its representative
        if (fitting_species == species.end()) {
            species.push_back({network});
            fitting_species = std::prev(species.end(), 1);
        }

        //finally, add the network to the found/created species
        fitting_species->networks.push_back(network);
    }

    //set last_innovation values
    for (Species &s: species) {
        if (!s.networks.empty() && s.networks[0].getFitness() > s.last_innovation_fitness) {
            s.last_innovation_fitness = s.networks[0].getFitness();
            s.last_innovation_generation = generation_number;
        }
    }

    //remove extinct species or species that haven't innovated in a while (but never the top species)
    species.remove_if([&](const Species &s) {
        return s.networks.empty() ||
               (s.last_innovation_generation + species_stagnation_threshold < generation_number
                && s.last_innovation_fitness != last_innovation_fitness);
    });

    //remove all species but top 2 if there is stagnation in the entire population
    if (last_innovation_generation + population_stagnation_threshold < generation_number) {
        species.sort([](const Species &s1, const Species &s2) {
            return s1.last_innovation_fitness > s2.last_innovation_fitness;
        });
        while (species.size() > 2) {
            species.pop_back();
        }
        last_innovation_generation = generation_number;
    }
}

void Neat_Instance::run_neat_helper(const std::function<void()> &evalNetworks) {
    std::cout << "Starting run_neat_helper\n";

    //prime the pump by evaluating, sorting and speciating the initial networks
    std::cout << "Evaluating initial networks\n";
    evalNetworks();

    std::cout << "Sorting initial networks by fitness\n";
    std::sort(networks.begin(), networks.end(), [&](const Network &n1, const Network &n2) {
        return
                n1.getFitness() * pow(float(n1.getNodes().size()), node_count_exponent)
                >
                n2.getFitness() * pow(float(n2.getNodes().size()), node_count_exponent);
    });

    std::cout << "Assigning initial networks to species\n";
    assign_networks_to_species();

    std::cout << "Entering main generation loop\n";
    while (generation_number++ < generation_target) {
        std::cout << "Generation " << generation_number << " starting\n";

        //the best network is chosen as species representative for the generation about to be calculated
        std::cout << "Setting species representatives\n";
        for (Species &s: species) {
            s.representative = s.networks[0];
        }

        //fitness sharing to assign each species an evolutionary value
        std::cout << "Calculating fitness sharing\n";
        float fitness_total = 0.f;
        for (Species &s: species) {
            s.avg_fitness = 0.f;
            for (Network &n: s.networks) {
                fitness_total += n.getFitness() / float(s.networks.size());
                s.avg_fitness += n.getFitness() / float(s.networks.size());
            }
        }

        //calculate the total number of networks to be eliminated
        std::cout << "Calculating elimination total\n";
        int elimination_total = std::accumulate(
                species.begin(), species.end(), 0,
                [this](int sum, const Species &s) {
                    return sum + int(float(s.networks.size()) * elimination_percentage);
                }
        );

        //clear the network list so the next generation may be added to it
        std::cout << "Clearing network list for new generation\n";
        networks.clear();

        std::cout << "Processing each species for reproduction\n";
        for (Species &s: species) {
            //each species eliminates the required amount of members. The integer cast ensures at least 1 member will remain
            int elimination = int(float(s.networks.size()) * elimination_percentage);
            //each species receives offspring according to its fitness
            int offspring = int(s.avg_fitness / fitness_total * float(elimination_total));

            std::cout << "Species processing: elimination=" << elimination << ", offspring=" << offspring << "\n";

            //eliminate weakest members (list is already sorted)
            std::cout << "Eliminating weakest members\n";
            s.networks.erase(s.networks.end() - elimination, s.networks.end());

            //if species is large enough, immediately push an unmodified champion to the main network list without mutating
            if (s.networks.size() > 2) {
                std::cout << "Pushing champion to new generation\n";
                networks.push_back(s.networks[0]);
                offspring--;
            }

            //refill with offspring
            std::cout << "Refilling species with offspring\n";
            for (int i = 0; i < s.networks.size() + offspring; i++) {
                if (rnd_f(0.f, 1.f) > offspring_mating_percentage || s.networks.size() == 1) {
                    //mutation without crossover
                    Network n = s.networks[GetRandomValue(0, int(s.networks.size()) - 1)];
                    n.mutate();
                    networks.push_back(n);
                } else {
                    //crossover
                    //pick a random father from the non-new networks (not the last one)
                    int father = GetRandomValue(0, int(s.networks.size()) - 2);
                    //pick a random mother with higher index from the non-new networks
                    int mother = GetRandomValue(father + 1, int(s.networks.size()) - 1);
                    Network n = Network::reproduce(s.networks[mother], s.networks[father]);
                    n.mutate();
                    networks.push_back(n);
                }
            }
        }

        //refill rounding errors with interspecies reproduction
        std::cout << "Refilling rounding errors with interspecies reproduction\n";
        while (networks.size() < population) {
            int father = GetRandomValue(0, int(networks.size() - 2));
            int mother = GetRandomValue(father + 1, int(networks.size() - 1));
            Network n = Network::reproduce(networks[mother], networks[father]);
            networks.push_back(n);
        }

        //re-calculate nodes in use

        //every 10 algo-steps, re-evaluate which nodes are actively used by networks to allow re-using of smaller indices
        // (limiting size of innovation list, since old connections can also be reused)
        //none of these old connections can currently be in use, else their start & end points would also be in use
        if (generation_number % 10 == 0) {
            std::cout << "Re-evaluating used nodes (generation " << generation_number << ")\n";
            for (Node_Gene &n: node_genes) {
                n.used = false;
            }
            for (Network &n: networks) {
                for (const auto &[id, c]: n.getConnections()) {
                    node_genes[c.gene.start].used = true;
                    node_genes[c.gene.end].used = true;
                }
            }
        }

        //calculate fitness for every network
        std::cout << "Evaluating fitness for new generation\n";
        evalNetworks();

        //sort by descending fitness -> all later species will be sorted
        std::cout << "Sorting new generation by fitness\n";
        std::sort(networks.begin(), networks.end(), [&](const Network &n1, const Network &n2) {
            return
                    n1.getFitness() * pow(float(n1.getNodes().size()), node_count_exponent)
                    >
                    n2.getFitness() * pow(float(n2.getNodes().size()), node_count_exponent);
        });

        //set last_innovation values
        std::cout << "Updating last innovation values\n";
        if (networks[0].getFitness() > last_innovation_fitness) {
            last_innovation_fitness = networks[0].getFitness();
            last_innovation_generation = generation_number;
        }

        //speciate networks
        std::cout << "Speciating networks\n";
        assign_networks_to_species();

        //save values
        if (generation_number % save_intervall == 0 && !folderpath.empty()) {
            std::cout << "Saving generation " << generation_number << "\n";
            save();
        }
        std::cout << "Printing generation summary\n";
        print();
    }
    std::cout << "run_neat_helper completed\n";
}

Node_Gene Neat_Instance::request_node_gene(Connection_Gene split) {
    //TODO: Return nodes already used by other networks
    //check if there is an unused node gene
    auto unused_node = std::find_if(node_genes.begin(), node_genes.end(), [](const Node_Gene &ng) { return !ng.used; });
    node_id id;
    //select either an unused node or create a new one
    if (unused_node == node_genes.end()) {
        id = node_id(node_genes.size());
        node_genes.push_back({id, 0.f, 0.f, true});
    } else {
        id = unused_node->id;
    }
    //set the (new) value for the (new) node_gene
    node_genes[id].x = (node_genes[split.start].x + node_genes[split.end].x) / 2.f;
    node_genes[id].y = (node_genes[split.start].y + node_genes[split.end].y) / 2.f + rnd_f(0.f, 0.1f);
    node_genes[id].used = true;
    //return the result
    return node_genes[id];
}

Node_Gene Neat_Instance::request_node_gene(node_id id) {
    return node_genes[id];
}


Connection_Gene Neat_Instance::request_connection_gene(node_id start, node_id end) {
    Connection_Gene ng = {connection_id(connection_genes.size()), start, end};
    if (connection_genes.find(ng) == connection_genes.end()) {
        connection_genes.insert(ng);
    }
    return *connection_genes.find(ng);
}

Connection_Gene Neat_Instance::request_connection_gene(connection_id id) {
    return *std::find_if(
            connection_genes.begin(), connection_genes.end(),
            [id](const Connection_Gene &cg) { return cg.id == id; }
    );
}

void Neat_Instance::print() {
    std::cout << "-------- Generation " << generation_number << " --------\n";

    std::cout << "Best Network:           " << last_innovation_fitness << "\n";
    std::cout << "Best Network Gen:       " << last_innovation_generation << "\n";
    std::cout << "Total Nodes:            " << node_genes.size() << "\n";
    std::cout << "Total Connections:      " << connection_genes.size() << "\n";
    std::cout << "Species Count:          " << species.size() << "\n";
    std::cout << "Species Information:\n";
    printf("+-----+--------+-----+-----+-----+\n");
    printf("| Pop |  Best  | Gen | BNC | BCC |\n");
    printf("+-----+--------+-----+-----+-----+\n");
    for (Species &s: species) {
        printf(
                "| %3lu | %6.1f | %3d | %3lu | %3lu |\n",
                s.networks.size(),
                s.networks[0].getFitness(),
                s.last_innovation_generation,
                s.networks[0].getNodes().size(),
                s.networks[0].getConnections().size()
        );
    }
    printf("+-----+--------+-----+-----+-----+\n");
}

void Neat_Instance::save() const {
    //create folder if neccessary
    std::filesystem::create_directories(folderpath);
    string filepath;
    filepath.append(folderpath);
    filepath.append("/");
    filepath.append(split(folderpath, "/").back()); //name of directory for identification
    filepath.append("_g");
    filepath.append(std::to_string(generation_number));
    filepath.append("_f");
    filepath.append(std::to_string(int(last_innovation_fitness)));
    filepath.append(".neat");
    //open file
    std::ofstream savefile;
    savefile.open(filepath);
    //save general AI parameters
    savefile
            << input_count << "\n"
            << output_count << "\n"
            << repetitions << "\n"
            << generation_number << "\n"
            << generation_target << "\n"
            << population << "\n"
            << speciation_threshold << "\n";
    //used nodes is not saved -> on eventual run it would be updated soon enough
    //save connection gene data -> this allows networks to only save innovation numbers
    for (const Node_Gene &ng: node_genes) {
        savefile << ng.id << "|" << ng.x << "|" << ng.y << ";";
    }
    savefile << "\n";
    for (const Connection_Gene &cg: connection_genes) {
        savefile << cg.id << "|" << cg.start << "|" << cg.end << ";";
    }
    savefile << "\n";
    //save networks
    for (const Network &n: networks) {
        savefile << n.to_string() << "\n";
    }
    savefile.close();
}

vector<Network> Neat_Instance::get_networks_sorted() {
    std::sort(networks.begin(), networks.end(),
              [](const Network &n1, const Network &n2) { return n1.getFitness() > n2.getFitness(); });
    return networks;
}

