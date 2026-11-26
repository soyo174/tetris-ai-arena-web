let request_id = Str.regexp "\"request_id\"[ \t]*:[ \t]*\"\\([0-9]+\\)\""

let () =
  try
    while true do
      let line = read_line () in
      match Str.search_forward request_id line 0 with
      | _ ->
          Printf.printf "{\"type\":\"PLAY\",\"request_id\":\"%s\",\"commands\":[\"HARD_DROP\"]}\n%!"
            (Str.matched_group 1 line)
      | exception Not_found -> ()
    done
  with End_of_file -> ()
