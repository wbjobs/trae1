import random
import copy
from typing import List, Tuple, Dict, Any
from utils.mutations import Mutator


class Chromosome:
    def __init__(self, data: bytes, mutation_type: str = None):
        self.data = data
        self.mutation_type = mutation_type
        self.fitness = 0.0
        self.timestamp = None

    def __repr__(self):
        return f"Chromosome(len={len(self.data)}, fitness={self.fitness:.2f})"


class GeneticStrategy:
    def __init__(
        self,
        mutator: Mutator,
        population_size: int = 20,
        mutation_rate: float = 0.3,
        crossover_rate: float = 0.7,
        elitism_count: int = 3
    ):
        self.mutator = mutator
        self.population_size = population_size
        self.mutation_rate = mutation_rate
        self.crossover_rate = crossover_rate
        self.elitism_count = elitism_count
        self.population: List[Chromosome] = []
        self.generation = 0
        self.best_fitness_history: List[float] = []
        self.mutation_success_count: Dict[str, int] = {
            'bit_flip': 0,
            'boundary': 0,
            'random_byte': 0,
            'length_overflow': 0
        }
        self.mutation_total_count: Dict[str, int] = {
            'bit_flip': 0,
            'boundary': 0,
            'random_byte': 0,
            'length_overflow': 0
        }

    def initialize_population(self, seed_packets: List[bytes]):
        self.population = []
        if not seed_packets:
            return

        for _ in range(self.population_size):
            seed = random.choice(seed_packets)
            mutated_data, mutation_type = self._mutate_with_tracking(seed)
            self.population.append(Chromosome(mutated_data, mutation_type))

    def _mutate_with_tracking(self, data: bytes) -> Tuple[bytes, str]:
        mutations = list(self.mutator.mutation_weights.keys())
        weights = list(self.mutator.mutation_weights.values())
        mutation_type = random.choices(mutations, weights=weights, k=1)[0]
        
        mutation_methods = {
            'bit_flip': self.mutator.bit_flip,
            'boundary': self.mutator.boundary_value,
            'random_byte': self.mutator.random_byte,
            'length_overflow': self.mutator.length_overflow
        }
        
        self.mutation_total_count[mutation_type] += 1
        return mutation_methods[mutation_type](data), mutation_type

    def evaluate_fitness(self, chromosome: Chromosome, triggered_crash: bool, response_time: float, has_exception: bool):
        fitness = 0.0
        
        if triggered_crash:
            fitness += 100.0
            if chromosome.mutation_type:
                self.mutation_success_count[chromosome.mutation_type] += 1
        elif has_exception:
            fitness += 50.0
            if chromosome.mutation_type:
                self.mutation_success_count[chromosome.mutation_type] += 1
        
        if response_time > 5.0:
            fitness += 20.0
        elif response_time > 2.0:
            fitness += 10.0
        elif response_time > 0.5:
            fitness += 5.0
        
        fitness += min(len(chromosome.data) / 100.0, 10.0)
        
        chromosome.fitness = fitness
        return fitness

    def selection(self) -> List[Chromosome]:
        total_fitness = sum(c.fitness for c in self.population)
        if total_fitness == 0:
            return random.sample(self.population, len(self.population))
        
        selected = []
        for _ in range(len(self.population) - self.elitism_count):
            r = random.uniform(0, total_fitness)
            current_sum = 0
            for chromosome in self.population:
                current_sum += chromosome.fitness
                if current_sum >= r:
                    selected.append(copy.deepcopy(chromosome))
                    break
        
        return selected

    def crossover(self, parent1: Chromosome, parent2: Chromosome) -> Tuple[Chromosome, Chromosome]:
        if random.random() > self.crossover_rate:
            return parent1, parent2
        
        data1 = list(parent1.data)
        data2 = list(parent2.data)
        
        min_len = min(len(data1), len(data2))
        if min_len < 2:
            return parent1, parent2
        
        point = random.randint(1, min_len - 1)
        
        child1_data = data1[:point] + data2[point:]
        child2_data = data2[:point] + data1[point:]
        
        child1 = Chromosome(bytes(child1_data), parent1.mutation_type)
        child2 = Chromosome(bytes(child2_data), parent2.mutation_type)
        
        return child1, child2

    def mutate(self, chromosome: Chromosome) -> Chromosome:
        if random.random() > self.mutation_rate:
            return chromosome
        
        mutated_data, mutation_type = self._mutate_with_tracking(chromosome.data)
        chromosome.data = mutated_data
        chromosome.mutation_type = mutation_type
        
        return chromosome

    def create_next_generation(self) -> List[bytes]:
        self.population.sort(key=lambda c: c.fitness, reverse=True)
        
        best_fitness = self.population[0].fitness if self.population else 0.0
        self.best_fitness_history.append(best_fitness)
        
        next_generation = []
        elites = self.population[:self.elitism_count]
        next_generation.extend(copy.deepcopy(elite) for elite in elites)
        
        selected = self.selection()
        
        for i in range(0, len(selected), 2):
            if i + 1 < len(selected):
                child1, child2 = self.crossover(selected[i], selected[i + 1])
                child1 = self.mutate(child1)
                child2 = self.mutate(child2)
                next_generation.extend([child1, child2])
            else:
                child = self.mutate(selected[i])
                next_generation.append(child)
        
        while len(next_generation) < self.population_size:
            next_generation.append(copy.deepcopy(random.choice(elites)))
        
        self.population = next_generation[:self.population_size]
        self.generation += 1
        
        self._update_mutation_weights()
        
        return [c.data for c in self.population]

    def _update_mutation_weights(self):
        new_weights = {}
        for mutation in self.mutator.mutation_weights:
            total = self.mutation_total_count.get(mutation, 0)
            success = self.mutation_success_count.get(mutation, 0)
            
            if total > 0:
                success_rate = success / total
                new_weights[mutation] = 1.0 + success_rate * 3.0
            else:
                new_weights[mutation] = 1.0
        
        self.mutator.mutation_weights = new_weights

    def get_stats(self) -> Dict[str, Any]:
        return {
            'generation': self.generation,
            'population_size': len(self.population),
            'best_fitness': max(c.fitness for c in self.population) if self.population else 0.0,
            'avg_fitness': sum(c.fitness for c in self.population) / len(self.population) if self.population else 0.0,
            'mutation_weights': self.mutator.mutation_weights.copy(),
            'mutation_success': self.mutation_success_count.copy(),
            'mutation_total': self.mutation_total_count.copy(),
            'best_fitness_history': self.best_fitness_history[-10:]
        }
