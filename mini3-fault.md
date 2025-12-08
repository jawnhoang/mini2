# Payload Fault Tolerant Streaming with gRPC 

Here we are, the final mini. We have covered internal optimization and
peer communication using gRPC. For the final discovery project we are 
looking at fault tolerance and revovery in data streaming and storage. 

This mini picks up where mini 2 left off in both code and data by
extending the exploration of services to fault tolerance. What do we mean
by fault tolerance can be wide topic area. Are we talking about service
recovery? Data mirroring? Reachability due to partioning? Or from a client
perspective for unenterrupted service?

Where to start? That is an interesting question that I leave up to the
teams. What do you want to explore? I've mentioned a couple areas above,
but there are more to consider. 


## Mini Goals

Explore a subject area of fault tolerance that you are interested in 
learning about. That simple, you choose.


### Basecamp

To expedite the project you can choose to start with mini 2. A key here is 
matching your network graph to help facilitiating testing your ideas. Consider
mini 2's graph, it was constructed to force issues of balance through a nested
set of servers with more than one path (cycle) to reach the lowest leaves. So,
depending on your choices, a modification to the graph might be needed to model
networks to faciliate your research.


#### Features

   * The language selection is open to all options.
   * Continue to think strategically and organize your code code for library 
     deployment as well as application work.
   * Continue to practice running from a shell (e.g., bash)
   * Testing harnesses are going to up to you to find the right combination to
     verify your design's weaknesses and strengths.


#### Avoidance hints

   * Again, cannot stress this enough, do not use the IDE to run your code.
   * Also, details still matters so, don't focus on making an application.  
   * Work from a stable base, before adding complexity ensure the baseline is
     functional. Does the network send/receive, store, or whatever correctly.
     Then and only then start introducing your algorithms/changes.


### Discovery

Search papers, alternate platforms to mimic, Is blockchain or elastic-like
services an inspiration?  


## Technical Constraints

Let us recap the guardrails mentioned above:

   * No language constrains
   * Continue with minimal third party libraries. Please do not use Anaconda 
     stuff; objectives are to understand how things are done.
   * No UX/UI stuff either. This includes test-based CLI menu systems.
   * No IDE VMs

