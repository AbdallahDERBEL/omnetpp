//
// This file is part of an OMNeT++/OMNEST simulation example.
//
// Copyright (C) 1992-2008 Andras Varga
//
// This file is distributed WITHOUT ANY WARRANTY. See the file
// `license' for details on this and other legal matters.
//


simple RandomGraphNode extends Node
{
    gates:
        inout g[];
}

network RandomGraph
{
    parameters:
        int n;
        double connectedness = default(0.15); // 0.0<x<1.0
    submodules:
        node[n]: RandomGraphNode;
    connections:
        for i=0..n-1, for j=0..n-1 {
            node[i].g++ <--> node[j].g++ if i!=j && uniform(0,1)<connectedness;
        }
}


