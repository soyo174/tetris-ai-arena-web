(doseq [line (line-seq (java.io.BufferedReader. *in*))]
  (when-let [[_ id] (re-find #"\"request_id\"\s*:\s*\"([0-9]+)\"" line)]
    (println (str "{\"type\":\"PLAY\",\"request_id\":\"" id
                  "\",\"commands\":[\"HARD_DROP\"]}"))
    (flush)))
