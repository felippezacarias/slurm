# -*- coding: utf-8 -*-

class Grafo:
    '''Classe usada para modelar um grafo'''
    def __init__(self):
        self.lista_vertices = {}

    def existe_vertice(self,vertice):
	"""Retorna se o vértice está no grafo."""
	if vertice in self.lista_vertices:
		return True
	return False

    def vazio(self):
	"""Retorna se o grafo é vazio."""
	if not self.lista_vertices:
		return True
	return False
    
    def add_vertice(self, vertice):
	"""Adiciona um vértice ao grafo."""
	if not self.existe_vertice(vertice):        
		self.lista_vertices[vertice] = {}
    
    def add_aresta(self,vertice1, vertice2, peso):
	"""Adiciona uma aresta ao grafo."""
	if not self.existe_vertice(vertice1):
		self.add_vertice(vertice1)
	if not self.existe_vertice(vertice2):
		self.add_vertice(vertice2)

	self.lista_vertices[vertice1][vertice2] = peso
	self.lista_vertices[vertice2][vertice1] = peso

    def atualiza_aresta(self,vertice1, vertice2, peso):
	"""Atualiza o valor de uma aresta do grafo."""
	self.lista_vertices[vertice1][vertice2] = peso

    
    def arestas(self, vertice):
	"""Retorna todos os vértices que possuem aresta
	   com o parametro vértice."""
        if self.existe_vertice(vertice):
            return self.lista_vertices[vertice]
        else:
            return {}

    def todas_arestas(self):
	"""Retorna todos as arestas do grafo."""
	list_tupla = []
	for v in self.vertices():
		aresta = self.arestas(v)
		lista = aresta.items()
		for (x,y) in lista:
			list_tupla.append((v,x,y))
	return list_tupla

    def todas_arestas_sorted_min(self):
	"""Retorna todos as arestas do grafo em ordem
	   crescente de degradação."""
	vertices = self.vertices()
	list_tupla = []
	adicionados = []
	for v in vertices:
		adicionados.append(v)
		aresta = self.arestas(v)
		lista = aresta.items()
		for (x,y) in lista:
			if (x not in adicionados):
				list_tupla.append((v,x,y))
	lista = sorted(list_tupla, key=lambda tup: tup[2])
	return lista

    def todas_arestas_sorted_max(self):
	"""Retorna todos as arestas do grafo em ordem
	   decrescente de degradação."""
	vertices = self.vertices()
	list_tupla = []
	adicionados = []
	for v in vertices:
		adicionados.append(v)
		aresta = self.arestas(v)
		lista = aresta.items()
		for (x,y) in lista:
			if (x not in adicionados):
				list_tupla.append((v,x,y))
	lista = sorted(list_tupla, key=lambda tup: tup[2], reverse=True)
	return lista

    def vertices(self):
	"""Retorna todos os vértices do grafo."""
        return self.lista_vertices.keys()

    def vizinhos(self, vertice):
	"""Retorna todos os vizinhos do vértices no grafo."""
        return self.lista_vertices[vertice].keys()

    def peso_aresta(self, vertice1, vertice2):
	"""Retorna o peso da aresta entre 2 vértices"""
        if self.existe_vertice(vertice1):
            if vertice2 in self.lista_vertices[vertice1]:
	            return self.lista_vertices[vertice1][vertice2]
        else:
            return None

    def deleta_aresta(self, vertice1, vertice2):
	"""Remove a aresta entre dois vértices do grafo."""
	if self.existe_vertice(vertice1) and self.existe_vertice(vertice2):
        	del self.lista_vertices[vertice1][vertice2]
        	del self.lista_vertices[vertice2][vertice1]
    
    def deleta_vertice(self, vertice):
	"""Remove um vértice do grafo."""
	arestas = self.lista_vertices[vertice]
        for vertice2 in arestas.keys():
            self.deleta_aresta(vertice, vertice2)
        del self.lista_vertices[vertice]

    def __str__(self):
	"""Percorre todas as combinações de vértices
	   entre início e fim."""
	retstr = ""
	for vertice in self.lista_vertices:
		retstr += "vertice:"
		retstr += "\n\t"
		retstr += vertice
		retstr += "\n\t"
		retstr += str(self.lista_vertices[vertice])
		retstr += "\n"
	return retstr
