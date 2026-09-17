Measurements behind the paper's figures and numbers.

Redraw the figures and print the summary tables with:
  pip install jupyter nbconvert pandas matplotlib numpy
  cd .. && gunzip -k measurements/*.gz && mkdir -p aggregated \
    && mv measurements/*.csv aggregated/ && jupyter nbconvert --execute scripts/plots.ipynb