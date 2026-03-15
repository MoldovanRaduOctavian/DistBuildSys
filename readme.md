# Distributed C/C++ Build System Disertation Thesis
This is the initial repository structure for the disertation project. More useful information will be put in here later on.


Include parser redesign

A single recursive function which first processes its input file and all the nested #include s 
recursively:
    See if the file exists
    Go through the list of parsed include directives
    Handle each include directive based on its type: angle/quotes
